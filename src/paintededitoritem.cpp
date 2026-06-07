#include "paintededitoritem.h"

#include "documentsession.h"
#include "editorconfig.h"

#include <QClipboard>
#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QThread>
#include <QWheelEvent>

namespace {
constexpr auto kInputMethodUpdateQueries =
    Qt::ImCursorRectangle | Qt::ImSurroundingText | Qt::ImCurrentSelection;

const EditorConfig &editorConfig()
{
    return EditorConfig::instance();
}

}

PaintedEditorItem::PaintedEditorItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_pasteLimitBytes(editorConfig().defaultPasteLimitBytes())
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemIsFocusScope, false);
    setAntialiasing(false);
    setOpaquePainting(true);
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setPerformanceHint(QQuickPaintedItem::FastFBOResizing, true);

    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(editorConfig().longPressMs());
    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (!m_mousePressed || m_dragStarted) {
            return;
        }
        emit menuRequested(m_pressPoint.x(), m_pressPoint.y());
    });

    m_highlightRefreshTimer.setSingleShot(true);
    m_highlightRefreshTimer.setInterval(editorConfig().highlightRefreshMs());
    connect(&m_highlightRefreshTimer, &QTimer::timeout, this, [this]() {
        if (viewportMoving()) {
            m_highlightRefreshTimer.start();
            return;
        }
        refreshVisibleMatchCache();
        update();
    });

    m_horizontalMetricsTimer.setSingleShot(true);
    m_horizontalMetricsTimer.setInterval(editorConfig().horizontalMetricsRefreshMs());
    connect(&m_horizontalMetricsTimer, &QTimer::timeout, this, [this]() {
        updateHorizontalMetrics();
    });

}

void PaintedEditorItem::paint(QPainter *painter)
{
    painter->fillRect(boundingRect(), QColor(QStringLiteral("#0a1528")));

    if (!occupied() || totalLines() <= 0) {
        return;
    }

    DocumentSession *doc = document();
    if (!doc) {
        return;
    }

    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(editorConfig().textPixelSize());
    painter->setFont(font);
    QFontMetrics fm(font);
    const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
    const qreal viewportWidth = textViewportWidth();

    const int numberWidth = lineNumberWidth();
    const qreal textBaseLeft = textAreaLeft();
    const qreal textLeft = textBaseLeft - m_horizontalOffset;
    const int lineCount = visibleLineCount();
    const bool selected = hasSelection();
    const int selStart = selectionStart();
    const int selEnd = selectionEnd();
    const QString query = searchQuery();
    const bool hasQuery = !query.isEmpty();
    const bool supportsInlineHighlight =
        hasQuery && !query.contains(QLatin1Char('\n')) && !query.contains(QLatin1Char('\r'));
    const int queryLen = query.length();
    const int preeditCursor = qBound(0,
                                     m_preeditCursorInString < 0 ? m_preeditText.length() : m_preeditCursorInString,
                                     m_preeditText.length());
    const qreal preeditCursorDx = m_preeditText.isEmpty()
        ? 0.0
        : static_cast<qreal>(fm.horizontalAdvance(m_preeditText.left(preeditCursor)));
    const bool moving = viewportMoving();
    const QVector<int> *visibleMatches = nullptr;
        if (supportsInlineHighlight && queryLen > 0 && lineCount > 0) {
        if (!moving) {
            refreshVisibleMatchCache();
            visibleMatches = &m_cachedVisibleMatches;
        } else if (!m_highlightRefreshTimer.isActive()) {
            requestHighlightRefreshTimerStart();
        }
    }
    int matchCursor = 0;
    int frameHighlightBudget = moving ? editorConfig().highlightFrameBudgetScrolling() : editorConfig().highlightFrameBudgetIdle();
    if (queryLen <= 1) {
        frameHighlightBudget = qMin(frameHighlightBudget, moving ? 120 : 360);
    }
    for (int i = 0; i < lineCount; ++i) {
        const int lineIndex = m_firstVisibleLine + i;
        if (lineIndex < 0 || lineIndex >= totalLines()) {
            break;
        }

        const int y = i * editorConfig().lineHeight();
        QRect lineRect(0, y, static_cast<int>(width()), editorConfig().lineHeight());

        if (lineIndex == m_cursorLine) {
            painter->fillRect(lineRect, QColor(QStringLiteral("#163b63")));
        } else if (lineIndex + 1 == currentLine()) {
            painter->fillRect(lineRect, QColor(QStringLiteral("#102b48")));
        }

        painter->setPen(QColor(QStringLiteral("#7087ad")));
        painter->drawText(QRect(8, y, numberWidth - 12, editorConfig().lineHeight()),
                          Qt::AlignRight | Qt::AlignVCenter,
                          QString::number(lineIndex + 1));

        painter->save();
        painter->setClipRect(QRectF(textBaseLeft, y, qMax<qreal>(1.0, width() - textBaseLeft), editorConfig().lineHeight()));

        const int lineStart = doc->lineStartOffset(lineIndex);
        const int lineLength = lineLengthAt(lineIndex);
        const bool useApproxLine = lineLength > editorConfig().longLineApproxThreshold();
        QString text;
        if (!useApproxLine && lineLength > 0) {
            text = lineTextAt(lineIndex);
        }
        auto xForColFast = [&](int column) -> qreal {
            const int safeColumn = qBound(0, column, lineLength);
            if (useApproxLine) {
                return static_cast<qreal>(safeColumn) * monoCharWidth;
            }
            return static_cast<qreal>(fm.horizontalAdvance(text, safeColumn));
        };

        if (supportsInlineHighlight
            && visibleMatches
            && queryLen > 0
            && lineLength > 0
            && frameHighlightBudget > 0) {
            const int lineEnd = lineStart + lineLength;
            int highlighted = 0;
            int visibleStartCol = 0;
            int visibleEndCol = lineLength;
            if (useApproxLine) {
                visibleStartCol = qBound(0,
                                         static_cast<int>(m_horizontalOffset / monoCharWidth)
                                             - editorConfig().longLineHighlightOverscanColumns(),
                                         lineLength);
                const int visibleCols = qMax(1,
                                             static_cast<int>(viewportWidth / monoCharWidth)
                                                 + 2 * editorConfig().longLineHighlightOverscanColumns());
                visibleEndCol = qBound(visibleStartCol, visibleStartCol + visibleCols, lineLength);
            }

            while (matchCursor < visibleMatches->size()
                   && visibleMatches->at(matchCursor) < lineStart) {
                ++matchCursor;
            }

            for (int idx = matchCursor; idx < visibleMatches->size(); ++idx) {
                const int absolutePos = visibleMatches->at(idx);
                if (absolutePos >= lineEnd) {
                    break;
                }
                const int matchPos = absolutePos - lineStart;
                if (matchPos < 0 || matchPos >= lineLength) {
                    continue;
                }
                const int matchEnd = qMin(lineLength, matchPos + queryLen);
                if (matchEnd <= matchPos) {
                    continue;
                }
                if (useApproxLine && (matchEnd < visibleStartCol || matchPos > visibleEndCol)) {
                    continue;
                }
                const qreal sx = textLeft + xForColFast(matchPos);
                const qreal ex = textLeft + xForColFast(matchEnd);
                const QRectF matchRect(sx, y + 7, qMax<qreal>(2.0, ex - sx), editorConfig().lineHeight() - 14);
                painter->fillRect(matchRect, QColor(QStringLiteral("#2c5d3e")));

                ++highlighted;
                --frameHighlightBudget;
                if (highlighted >= editorConfig().maxFindHighlightsPerLine()) {
                    break;
                }
                if (frameHighlightBudget <= 0) {
                    break;
                }
            }
        }

        if (selected) {
            const int lineTextEnd = lineStart + lineLength;
            const int highlightStart = qMax(selStart, lineStart);
            const int highlightEnd = qMin(selEnd, lineTextEnd);
            if (highlightEnd > highlightStart) {
                const int startCol = highlightStart - lineStart;
                const int endCol = highlightEnd - lineStart;
                const qreal sx = textLeft + xForColFast(startCol);
                const qreal ex = textLeft + xForColFast(endCol);
                QRectF highlightRect(sx, y + 4, qMax<qreal>(2.0, ex - sx), editorConfig().lineHeight() - 8);
                painter->fillRect(highlightRect, QColor(QStringLiteral("#225c9b")));
            }
        }

        painter->setPen(QColor(QStringLiteral("#eaf1ff")));
        const int baseline = y + (editorConfig().lineHeight() + fm.ascent() - fm.descent()) / 2;
        QString drawText = text;
        qreal drawX = textLeft;
        if (useApproxLine && lineLength > 0) {
            const int startCol = qBound(0,
                                        static_cast<int>(m_horizontalOffset / monoCharWidth) - editorConfig().paintOverscanColumns(),
                                        lineLength);
            const int visibleCols = qMax(1,
                                         static_cast<int>(viewportWidth / monoCharWidth)
                                             + 2 * editorConfig().paintOverscanColumns());
            const int endCol = qBound(startCol, startCol + visibleCols, lineLength);
            drawText = doc->lineTextSliceAt(lineIndex, startCol, endCol - startCol);
            drawX = textLeft + static_cast<qreal>(startCol) * monoCharWidth;
        }
        painter->drawText(QPointF(drawX, baseline), drawText);

        if (hasActiveFocus() && !m_preeditText.isEmpty() && lineIndex == m_cursorLine) {
            const qreal preeditX = textLeft + m_cursorXInLine;
            const qreal preeditWidth = static_cast<qreal>(fm.horizontalAdvance(m_preeditText));
            painter->setPen(QColor(QStringLiteral("#f5d18f")));
            painter->drawText(QPointF(preeditX, baseline), m_preeditText);
            painter->drawLine(QPointF(preeditX, y + editorConfig().lineHeight() - 7),
                              QPointF(preeditX + preeditWidth, y + editorConfig().lineHeight() - 7));
        }
        painter->restore();
    }

    if (hasActiveFocus()) {
        const int drawLine = m_cursorLine - m_firstVisibleLine;
        if (drawLine >= 0 && drawLine < lineCount) {
            const qreal cx = textLeft + m_cursorXInLine + preeditCursorDx;
            const int cy = drawLine * editorConfig().lineHeight();
            painter->save();
            painter->setClipRect(QRectF(textBaseLeft, cy, qMax<qreal>(1.0, width() - textBaseLeft), editorConfig().lineHeight()));
            painter->fillRect(QRectF(cx, cy + 5, 1, editorConfig().lineHeight() - 10), QColor(QStringLiteral("#f0f6ff")));
            painter->restore();
        }
    }

}

QObject *PaintedEditorItem::documentSession() const
{
    return m_documentSession.data();
}

void PaintedEditorItem::setDocumentSession(QObject *documentSession)
{
    auto *session = qobject_cast<DocumentSession *>(documentSession);
    if (m_documentSession == session) {
        return;
    }
    if (m_documentSession) {
        disconnect(m_documentSession.data(), nullptr, this, nullptr);
    }
    m_documentSession = session;
    if (m_documentSession) {
        connect(m_documentSession.data(), &DocumentSession::lineCountChanged,
                this, &PaintedEditorItem::handleDocumentLineCountChanged);
        connect(m_documentSession.data(), &DocumentSession::textRevisionChanged,
                this, &PaintedEditorItem::handleDocumentTextRevisionChanged);
        connect(m_documentSession.data(), &DocumentSession::currentLineChanged,
                this, [this]() {
                    update();
                });
        connect(m_documentSession.data(), &DocumentSession::searchStateChanged,
                this, &PaintedEditorItem::handleDocumentSearchStateChanged);
        connect(m_documentSession.data(), &DocumentSession::editCapabilitiesChanged,
                this, &PaintedEditorItem::handleDocumentEditCapabilitiesChanged);
        connect(m_documentSession.data(), &DocumentSession::multiSelectEnabledChanged,
                this, [this]() {
                    update();
                });
    }
    resetDocumentViewState();
    emit documentSessionChanged();
    emit scrollMetricsChanged();
}

bool PaintedEditorItem::multiSelectEnabled() const
{
    DocumentSession *doc = document();
    return doc && doc->multiSelectEnabled();
}

bool PaintedEditorItem::occupied() const
{
    return document() != nullptr;
}

bool PaintedEditorItem::canEdit() const
{
    DocumentSession *doc = document();
    return doc && doc->canModify() && !m_editBlocked;
}

int PaintedEditorItem::totalLines() const
{
    DocumentSession *doc = document();
    return doc ? doc->lineCount() : 0;
}

int PaintedEditorItem::currentLine() const
{
    DocumentSession *doc = document();
    return doc ? doc->currentLine() : 0;
}

int PaintedEditorItem::textRevision() const
{
    DocumentSession *doc = document();
    return doc ? doc->textRevision() : 0;
}

QString PaintedEditorItem::searchQuery() const
{
    DocumentSession *doc = document();
    return doc ? doc->searchQuery() : QString();
}

bool PaintedEditorItem::editBlocked() const
{
    return m_editBlocked;
}

void PaintedEditorItem::setEditBlocked(bool blocked)
{
    if (m_editBlocked == blocked) {
        return;
    }
    m_editBlocked = blocked;
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    emit editBlockedChanged();
}

void PaintedEditorItem::resetDocumentViewState()
{
    m_firstVisibleLine = 0;
    m_cursorOffset = 0;
    m_cursorLine = 0;
    m_cursorColumn = 0;
    m_selectionAnchorOffset = -1;
    m_selectionCursorOffset = -1;
    m_horizontalOffset = 0.0;
    m_contentWidth = 0.0;
    m_preeditText.clear();
    m_preeditCursorInString = -1;
    m_horizontalMetricsTimer.stop();
    m_lastViewportMotionMs = 0;
    stopLongPress();
    stopAutoScroll();
    clearLineCache();
    invalidateHighlightCache(true);
    if (occupied()) {
        m_selectionAnchorOffset = m_cursorOffset;
        m_selectionCursorOffset = m_cursorOffset;
        syncCursorFromOffset();
        updateHorizontalMetrics();
    }
    update();
}

void PaintedEditorItem::handleDocumentLineCountChanged()
{
    setFirstVisibleLineInternal(m_firstVisibleLine);
    invalidateHighlightCache(false);
    syncCursorFromOffset();
    m_horizontalMetricsTimer.start();
    update();
    emit scrollMetricsChanged();
}

void PaintedEditorItem::handleDocumentTextRevisionChanged()
{
    clearLineCache();
    invalidateHighlightCache(false);
    syncCursorFromOffset();
    m_horizontalMetricsTimer.start();
    update();
}

void PaintedEditorItem::handleDocumentSearchStateChanged()
{
    invalidateHighlightCache(true);
    update();
}

void PaintedEditorItem::handleDocumentEditCapabilitiesChanged()
{
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
}

qreal PaintedEditorItem::scrollPosition() const
{
    const int maxFirst = maxFirstVisibleLine();
    if (maxFirst <= 0) {
        return 0.0;
    }
    return static_cast<qreal>(m_firstVisibleLine) / static_cast<qreal>(maxFirst);
}

void PaintedEditorItem::setScrollPosition(qreal position)
{
    position = qBound<qreal>(0.0, position, 1.0);
    const int maxFirst = maxFirstVisibleLine();
    const int target = qRound(position * static_cast<qreal>(maxFirst));
    setFirstVisibleLineInternal(target);
}

qreal PaintedEditorItem::scrollSize() const
{
    if (totalLines() <= 0) {
        return 1.0;
    }
    const qreal ratio = static_cast<qreal>(visibleLineCount()) / static_cast<qreal>(totalLines());
    return qBound<qreal>(0.05, ratio, 1.0);
}

qreal PaintedEditorItem::horizontalScrollPosition() const
{
    const qreal maxOffset = maxHorizontalOffset();
    if (maxOffset <= 0.0) {
        return 0.0;
    }
    return m_horizontalOffset / maxOffset;
}

void PaintedEditorItem::setHorizontalScrollPosition(qreal position)
{
    position = qBound<qreal>(0.0, position, 1.0);
    const qreal maxOffset = maxHorizontalOffset();
    setHorizontalOffsetInternal(position * maxOffset);
}

qreal PaintedEditorItem::horizontalScrollSize() const
{
    const qreal viewWidth = textViewportWidth();
    if (m_contentWidth <= viewWidth || viewWidth <= 0.0) {
        return 1.0;
    }
    return qBound<qreal>(0.05, viewWidth / m_contentWidth, 1.0);
}

int PaintedEditorItem::pasteLimitBytes() const
{
    return m_pasteLimitBytes;
}

void PaintedEditorItem::setPasteLimitBytes(int bytes)
{
    const EditorConfig &config = editorConfig();
    const int clamped = qBound(config.minPasteLimitBytes(), bytes, config.maxPasteLimitBytes());
    if (m_pasteLimitBytes == clamped) {
        return;
    }
    m_pasteLimitBytes = clamped;
    emit pasteLimitBytesChanged();
}

void PaintedEditorItem::selectByOffset(int offset)
{
    if (!occupied()) {
        return;
    }
    forceActiveFocus();
    setCursorOffset(offset, false, true);
}

void PaintedEditorItem::selectRange(int start, int length)
{
    if (!occupied()) {
        return;
    }

    DocumentSession *doc = document();
    if (!doc) {
        return;
    }

    const int total = doc->textLength();
    const int clampedStart = qBound(0, start, total);
    const int clampedEnd = qBound(0, start + qMax(0, length), total);
    if (m_selectionAnchorOffset == clampedStart
        && m_selectionCursorOffset == clampedEnd
        && m_cursorOffset == clampedEnd) {
        return;
    }

    forceActiveFocus();
    m_selectionAnchorOffset = clampedStart;
    m_selectionCursorOffset = clampedEnd;
    m_cursorOffset = clampedEnd;
    m_preferredColumn = -1;
    syncCursorFromOffset();
    ensureCursorVisible();
    doc->setCursorPosition(m_cursorOffset);
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
}

void PaintedEditorItem::performCopy()
{
    DocumentSession *doc = document();
    if (!doc || !occupied()) {
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return;
    }

    if (hasSelection()) {
        clipboard->setText(doc->textSlice(selectionStart(), selectionEnd() - selectionStart()));
        return;
    }

    clipboard->setText(lineTextAt(m_cursorLine));
}

void PaintedEditorItem::performCut()
{
    if (!occupied() || !ensureEditable()) {
        return;
    }

    DocumentSession *doc = document();
    if (!doc) {
        return;
    }

    if (hasSelection()) {
        performCopy();
        deleteCurrentSelection();
        return;
    }

    const QString line = lineTextAt(m_cursorLine);
    if (line.isEmpty()) {
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(line);
    }
    if (doc->deleteLineAt(m_cursorLine)) {
        clearLineCache();
        m_cursorOffset = clampedOffset(m_cursorOffset);
        clearSelectionToCursor();
        syncCursorFromOffset();
        doc->setCursorPosition(m_cursorOffset);
        update();
    }
}

void PaintedEditorItem::performPaste()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard || !occupied() || !ensureEditable()) {
        return;
    }

    const QString clip = clipboard->text();
    if (m_pasteLimitBytes > 0 && clip.toUtf8().size() > m_pasteLimitBytes) {
        const int kb = qMax(1, m_pasteLimitBytes / 1024);
        emit toastRequested(QStringLiteral("Paste exceeds the single-paste limit (%1 KB).").arg(kb));
        return;
    }

    insertText(clip);
}

void PaintedEditorItem::performDelete()
{
    if (!occupied() || !ensureEditable()) {
        return;
    }

    if (hasSelection()) {
        deleteCurrentSelection();
        return;
    }

    DocumentSession *doc = document();
    if (!doc) {
        return;
    }

    const int totalLength = doc->textLength();
    if (m_cursorOffset >= totalLength) {
        return;
    }

    const int removeLen = deleteRemoveLength();
    if (removeLen <= 0) {
        return;
    }

    applyTextEdit(m_cursorOffset, removeLen, QString());
}

void PaintedEditorItem::performUndo()
{
    DocumentSession *doc = document();
    if (!doc || !occupied()) {
        return;
    }

    if (!ensureEditable()) {
        return;
    }

    if (!doc->undo()) {
        emit toastRequested(QStringLiteral("Nothing to undo."));
        return;
    }

    clearLineCache();
    m_cursorOffset = clampedOffset(m_cursorOffset);
    clearSelectionToCursor();
    syncCursorFromOffset();
    ensureCursorVisible();
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
}

void PaintedEditorItem::performRedo()
{
    DocumentSession *doc = document();
    if (!doc || !occupied()) {
        return;
    }

    if (!ensureEditable()) {
        return;
    }

    if (!doc->redo()) {
        emit toastRequested(QStringLiteral("Nothing to redo."));
        return;
    }

    clearLineCache();
    m_cursorOffset = clampedOffset(m_cursorOffset);
    clearSelectionToCursor();
    syncCursorFromOffset();
    ensureCursorVisible();
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
}

void PaintedEditorItem::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChanged(newGeometry, oldGeometry);
    if (newGeometry.size() == oldGeometry.size()) {
        return;
    }
    setFirstVisibleLineInternal(m_firstVisibleLine);
    ensureCursorVisible();
    updateHorizontalMetrics();
    emit scrollMetricsChanged();
}

void PaintedEditorItem::mousePressEvent(QMouseEvent *event)
{
    if (!occupied() || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    forceActiveFocus();
    emit focusRequested();

    m_mousePressed = true;
    m_dragStarted = false;
    m_pressInsideSelection = false;
    m_pressPoint = event->pos();
    m_lastMousePoint = event->pos();
    m_pressOffset = offsetForPoint(event->pos());

    const bool keepSelection = (event->modifiers() & Qt::ShiftModifier);
    if (multiSelectEnabled() && hasSelection() && !keepSelection) {
        const int selStart = selectionStart();
        const int selEnd = selectionEnd();
        if (m_pressOffset >= selStart && m_pressOffset <= selEnd) {
            // Keep current selection while waiting for long-press menu.
            m_pressInsideSelection = true;
        }
    }
    if (!m_pressInsideSelection) {
        setCursorOffset(m_pressOffset, keepSelection, true);
    }

    startLongPress(event->pos());
    event->accept();
}

void PaintedEditorItem::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mousePressed || !occupied()) {
        event->ignore();
        return;
    }

    m_lastMousePoint = event->pos();
    if ((event->pos() - m_pressPoint).manhattanLength() >= editorConfig().selectionDragThreshold()) {
        m_dragStarted = true;
        stopLongPress();
    }

    if (multiSelectEnabled()) {
        if (m_pressInsideSelection && m_dragStarted) {
            // User starts dragging after pressing inside selection: start a new drag selection from press point.
            setCursorOffset(m_pressOffset, false, true);
            m_pressInsideSelection = false;
        }
        setCursorOffset(offsetForPoint(event->pos()), true, true);

        int verticalDirection = 0;
        if (event->pos().y() < 2.0) {
            verticalDirection = -1;
        } else if (event->pos().y() > height() - 2.0) {
            verticalDirection = 1;
        }

        const bool horizontalEdge =
            event->pos().x() < (textAreaLeft() + 2.0)
            || event->pos().x() > (width() - 2.0);

        if (verticalDirection != 0 || horizontalEdge) {
            startAutoScroll(verticalDirection);
        } else {
            stopAutoScroll();
        }
    } else {
        stopAutoScroll();
        const qreal dy = event->pos().y() - m_pressPoint.y();
        const int deltaLines = static_cast<int>(-dy / 12.0);
        if (deltaLines != 0) {
            setFirstVisibleLineInternal(m_firstVisibleLine + deltaLines);
            m_pressPoint = event->pos();
        }
    }

    event->accept();
}

void PaintedEditorItem::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    m_mousePressed = false;
    m_dragStarted = false;
    m_pressInsideSelection = false;
    stopLongPress();
    stopAutoScroll();
}

void PaintedEditorItem::wheelEvent(QWheelEvent *event)
{
    if (!occupied()) {
        event->ignore();
        return;
    }

    const QPoint angle = event->angleDelta();
    if ((event->modifiers() & Qt::ShiftModifier) || qAbs(angle.x()) > qAbs(angle.y())) {
        const int raw = (angle.x() != 0) ? angle.x() : angle.y();
        if (raw == 0) {
            event->ignore();
            return;
        }
        const int steps = qMax(1, qAbs(raw) / 120);
        const qreal stepPx = 40.0 * steps;
        setHorizontalOffsetInternal(m_horizontalOffset + ((raw > 0) ? -stepPx : stepPx));
        event->accept();
        return;
    }

    const QPoint numDegrees = angle / 8;
    if (numDegrees.y() == 0) {
        event->ignore();
        return;
    }

    const int lines = qMax(1, qAbs(numDegrees.y()) / 15);
    const int direction = numDegrees.y() > 0 ? -1 : 1;
    setFirstVisibleLineInternal(m_firstVisibleLine + direction * lines);
    event->accept();
}

void PaintedEditorItem::keyPressEvent(QKeyEvent *event)
{
    if (!occupied()) {
        event->ignore();
        return;
    }

    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool alt = event->modifiers() & Qt::AltModifier;
    const bool meta = event->modifiers() & Qt::MetaModifier;

    if (ctrl && event->key() == Qt::Key_C) {
        performCopy();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_X) {
        performCut();

        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_V) {
        performPaste();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_A) {
        DocumentSession *doc = document();
        if (doc) {
            m_selectionAnchorOffset = 0;
            m_selectionCursorOffset = doc->textLength();
            m_cursorOffset = m_selectionCursorOffset;
            syncCursorFromOffset();
            doc->setCursorPosition(m_cursorOffset);
            update();
        }
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_Z) {
        performUndo();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_F) {
        emit findRequested();
        event->accept();
        return;
    }
    if (ctrl && event->key() == Qt::Key_Y) {
        performRedo();
        event->accept();
        return;
    }

    if (!m_preeditText.isEmpty() && !ctrl && !alt && !meta) {
        event->ignore();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Backspace:
        if (hasSelection()) {
            deleteCurrentSelection();
        } else if (m_cursorOffset > 0) {
            const int removeLen = backspaceRemoveLength();
            if (removeLen > 0) {
                applyTextEdit(m_cursorOffset - removeLen, removeLen, QString());
            }
        }
        event->accept();
        return;
    case Qt::Key_Delete:
        performDelete();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insertText(preferredLineBreak());
        event->accept();
        return;
    case Qt::Key_Tab:
        insertText(QStringLiteral("\t"));
        event->accept();
        return;
    case Qt::Key_Left:
        moveHorizontal(-1, shift);
        event->accept();
        return;
    case Qt::Key_Right:
        moveHorizontal(1, shift);
        event->accept();
        return;
    case Qt::Key_Up:
        moveVertical(-1, shift);
        event->accept();
        return;
    case Qt::Key_Down:
        moveVertical(1, shift);
        event->accept();
        return;
    case Qt::Key_Home:
        moveToLineBoundary(true, shift);
        event->accept();
        return;
    case Qt::Key_End:
        moveToLineBoundary(false, shift);
        event->accept();
        return;
    case Qt::Key_PageUp:
        moveVertical(-visibleLineCount(), shift);
        event->accept();
        return;
    case Qt::Key_PageDown:
        moveVertical(visibleLineCount(), shift);
        event->accept();
        return;
    default:
        break;
    }

    if (!ctrl && !alt && !meta && !event->text().isEmpty() && event->text().at(0).isPrint()) {
        insertText(event->text());
        event->accept();
        return;
    }

    event->ignore();
}

void PaintedEditorItem::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    update();
    QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
}

void PaintedEditorItem::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    stopLongPress();
    stopAutoScroll();
    if (!m_preeditText.isEmpty() || m_preeditCursorInString != -1) {
        m_preeditText.clear();
        m_preeditCursorInString = -1;
    }
    update();
}

void PaintedEditorItem::inputMethodEvent(QInputMethodEvent *event)
{
    if (!occupied()) {
        event->ignore();
        return;
    }

    const QString commit = event->commitString();
    const QString preedit = event->preeditString();
    int replacementStart = event->replacementStart();
    int replacementLength = qMax(0, event->replacementLength());
    int preeditCursor = -1;
    for (const QInputMethodEvent::Attribute &attr : event->attributes()) {
        if (attr.type == QInputMethodEvent::Cursor) {
            preeditCursor = attr.start;
            break;
        }
    }

    // Some IMEs send negative replacementStart while committing plain text.
    // We do not maintain preedit text in-buffer, so replacing backward here
    // may delete real content (often the previous line break). Treat as insert.
    if (replacementStart < 0) {
        replacementStart = 0;
        replacementLength = 0;
    }

    if (!commit.isEmpty()) {
        if (!ensureEditable()) {
            m_preeditText.clear();
            m_preeditCursorInString = -1;
            QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
            update();
            event->accept();
            return;
        }

        const int start = clampedOffset(m_cursorOffset + replacementStart);
        applyTextEdit(start, replacementLength, commit);
    }

    if (m_preeditText != preedit || m_preeditCursorInString != preeditCursor) {
        m_preeditText = preedit;
        m_preeditCursorInString = preeditCursor;
        update();
    }
    QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    event->accept();
}

QVariant PaintedEditorItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    DocumentSession *doc = document();

    switch (query) {
    case Qt::ImEnabled:
        return occupied() && canEdit();
    case Qt::ImCursorRectangle: {
        const int drawLine = m_cursorLine - m_firstVisibleLine;
        qreal preeditDx = 0.0;
        if (!m_preeditText.isEmpty()) {
            QFont font(QStringLiteral("Consolas"));
            font.setPixelSize(editorConfig().textPixelSize());
            QFontMetrics fm(font);
            const int cursor = qBound(0,
                                      m_preeditCursorInString < 0 ? m_preeditText.length() : m_preeditCursorInString,
                                      m_preeditText.length());
            preeditDx = static_cast<qreal>(fm.horizontalAdvance(m_preeditText.left(cursor)));
        }
        QRectF rect(textAreaLeft() + m_cursorXInLine + preeditDx - m_horizontalOffset,
                    drawLine * editorConfig().lineHeight(),
                    1,
                    editorConfig().lineHeight());
        return mapRectToScene(rect);
    }
    case Qt::ImCursorPosition:
    case Qt::ImAbsolutePosition:
        return m_cursorOffset;
    case Qt::ImAnchorPosition:
        return hasSelection() ? selectionStart() : m_cursorOffset;
    case Qt::ImCurrentSelection:
        if (!doc || !hasSelection()) {
            return QString();
        }
        return doc->textSlice(selectionStart(), qMin(4096, selectionEnd() - selectionStart()));
    case Qt::ImSurroundingText:
        if (!doc) {
            return QString();
        }
        return doc->textSlice(qMax(0, m_cursorOffset - 2048),
                              qMin(4096, doc->textLength() - qMax(0, m_cursorOffset - 2048)));
    case Qt::ImTextBeforeCursor:
        if (!doc) {
            return QString();
        }
        return doc->textSlice(qMax(0, m_cursorOffset - 1024), qMin(1024, m_cursorOffset));
    case Qt::ImTextAfterCursor:
        if (!doc) {
            return QString();
        }
        return doc->textSlice(m_cursorOffset, qMin(1024, doc->textLength() - m_cursorOffset));
    default:
        return {};
    }
}

void PaintedEditorItem::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_autoScrollTimerId) {
        if (!m_mousePressed || !multiSelectEnabled()) {
            stopAutoScroll();
            return;
        }

        if (m_autoScrollDirection != 0) {
            setFirstVisibleLineInternal(m_firstVisibleLine + m_autoScrollDirection);
        }

        const qreal leftEdge = textAreaLeft() + 2.0;
        const qreal rightEdge = width() - 2.0;
        if (m_lastMousePoint.x() < leftEdge) {
            setHorizontalOffsetInternal(m_horizontalOffset - 24.0);
        } else if (m_lastMousePoint.x() > rightEdge) {
            setHorizontalOffsetInternal(m_horizontalOffset + 24.0);
        }

        setCursorOffset(offsetForPoint(m_lastMousePoint), true, true);
        return;
    }

    QQuickPaintedItem::timerEvent(event);
}

DocumentSession *PaintedEditorItem::document() const
{
    return m_documentSession.data();
}

int PaintedEditorItem::visibleLineCount() const
{
    return qMax(1, static_cast<int>(height()) / editorConfig().lineHeight() + 1);
}

int PaintedEditorItem::maxFirstVisibleLine() const
{
    return qMax(0, totalLines() - visibleLineCount());
}

int PaintedEditorItem::clampedOffset(int offset) const
{
    DocumentSession *doc = document();
    if (!doc) {
        return qMax(0, offset);
    }
    const int totalLength = doc->textLength();
    return qBound(0, offset, totalLength);
}

int PaintedEditorItem::lineNumberWidth() const
{
    const int digits = QString::number(qMax(1, totalLines())).size();
    if (digits != m_cachedLineNumberDigits) {
        QFont font(QStringLiteral("Consolas"));
        font.setPixelSize(editorConfig().textPixelSize() - 2);
        QFontMetrics fm(font);
        m_cachedLineNumberWidth = qMax(64, fm.horizontalAdvance(QString(digits, QLatin1Char('9'))) + 18);
        m_cachedLineNumberDigits = digits;
    }
    return m_cachedLineNumberWidth;
}

qreal PaintedEditorItem::textAreaLeft() const
{
    return lineNumberWidth() + 10.0;
}

int PaintedEditorItem::lineAtY(qreal y) const
{
    if (totalLines() <= 0) {
        return 0;
    }
    const int local = qBound(0, static_cast<int>(y) / editorConfig().lineHeight(), visibleLineCount() - 1);
    return qBound(0, m_firstVisibleLine + local, totalLines() - 1);
}

int PaintedEditorItem::offsetForPoint(const QPointF &point) const
{
    DocumentSession *doc = document();
    if (!doc || !occupied() || totalLines() <= 0) {
        return 0;
    }

    const int line = lineAtY(point.y());
    const qreal xInText = point.x() - textAreaLeft() + m_horizontalOffset;
    const int lineLength = lineLengthAt(line);

    int column = 0;
    if (lineLength > editorConfig().longLineApproxThreshold()) {
        QFont font(QStringLiteral("Consolas"));
        font.setPixelSize(editorConfig().textPixelSize());
        QFontMetrics fm(font);
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        column = qBound(0, static_cast<int>(xInText / monoCharWidth), lineLength);
    } else {
        const QString text = lineTextAt(line);
        column = columnForX(text, xInText);
    }

    const int lineStart = doc->lineStartOffset(line);
    return lineStart + column;
}

int PaintedEditorItem::offsetForLineColumn(int line, int column) const
{
    DocumentSession *doc = document();
    if (!doc || totalLines() <= 0) {
        return 0;
    }
    const int safeLine = qBound(0, line, totalLines() - 1);
    const int lineLength = lineLengthAt(safeLine);
    const int safeCol = qBound(0, column, lineLength);
    return doc->lineStartOffset(safeLine) + safeCol;
}

int PaintedEditorItem::columnForX(const QString &lineText, qreal xInText) const
{
    if (xInText <= 0.0 || lineText.isEmpty()) {
        return 0;
    }

    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(editorConfig().textPixelSize());
    QFontMetrics fm(font);
    if (lineText.length() > editorConfig().longLineApproxThreshold()) {
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        const int col = static_cast<int>(xInText / monoCharWidth);
        return qBound(0, col, lineText.length());
    }

    int low = 0;
    int high = lineText.length();
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        const int width = fm.horizontalAdvance(lineText.left(mid));
        if (width <= xInText) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
}

qreal PaintedEditorItem::xForColumn(const QString &lineText, int column) const
{
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(editorConfig().textPixelSize());
    QFontMetrics fm(font);
    const int safeColumn = qBound(0, column, lineText.length());
    if (lineText.length() > editorConfig().longLineApproxThreshold()) {
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        return static_cast<qreal>(safeColumn) * monoCharWidth;
    }
    return static_cast<qreal>(fm.horizontalAdvance(lineText.left(safeColumn)));
}

QString PaintedEditorItem::lineTextAt(int line) const
{
    DocumentSession *doc = document();
    if (!doc || !occupied() || line < 0 || line >= totalLines()) {
        return {};
    }

    const auto it = m_lineCache.constFind(line);
    if (it != m_lineCache.constEnd()) {
        return it.value();
    }

    const QString text = doc->lineTextAt(line);
    m_lineCache.insert(line, text);
    return text;
}

int PaintedEditorItem::lineLengthAt(int line) const
{
    DocumentSession *doc = document();
    if (!doc || !occupied() || line < 0 || line >= totalLines()) {
        return 0;
    }

    const auto it = m_lineLengthCache.constFind(line);
    if (it != m_lineLengthCache.constEnd()) {
        return it.value();
    }

    const int length = qMax(0, doc->lineLengthAt(line));
    m_lineLengthCache.insert(line, length);
    return length;
}

void PaintedEditorItem::clearLineCache()
{
    m_lineCache.clear();
    m_lineLengthCache.clear();
    m_lineWidthCache.clear();
}

void PaintedEditorItem::pruneLineCache()
{
    if (!occupied() || totalLines() <= 0) {
        clearLineCache();
        return;
    }

    const int visible = visibleLineCount();
    const int keepStart = qMax(0, m_firstVisibleLine - editorConfig().lineCacheMarginLines());
    const int keepEnd = qMin(totalLines() - 1, m_firstVisibleLine + visible + editorConfig().lineCacheMarginLines());

    for (auto it = m_lineCache.begin(); it != m_lineCache.end();) {
        const int line = it.key();
        if (line < keepStart || line > keepEnd) {
            it = m_lineCache.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_lineLengthCache.begin(); it != m_lineLengthCache.end();) {
        const int line = it.key();
        if (line < keepStart || line > keepEnd) {
            it = m_lineLengthCache.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_lineWidthCache.begin(); it != m_lineWidthCache.end();) {
        const int line = it.key();
        if (line < keepStart || line > keepEnd) {
            it = m_lineWidthCache.erase(it);
        } else {
            ++it;
        }
    }
}

void PaintedEditorItem::invalidateHighlightCache(bool clearMatches)
{
    m_highlightCacheDirty = true;
    m_highlightCacheFirstLine = -1;
    m_highlightCacheLastLine = -1;
    m_highlightCacheTextRevision = -1;
    m_highlightCacheQuery.clear();

    if (clearMatches) {
        m_cachedVisibleMatches.clear();
    }

    if (!occupied() || totalLines() <= 0 || searchQuery().isEmpty()) {
        m_highlightRefreshTimer.stop();
        return;
    }

    if (!m_highlightRefreshTimer.isActive()) {
        m_highlightRefreshTimer.start();
    }
}

void PaintedEditorItem::refreshVisibleMatchCache()
{
    if (!m_highlightCacheDirty) {
        return;
    }

    DocumentSession *doc = document();
    if (!doc || !occupied() || totalLines() <= 0) {
        m_cachedVisibleMatches.clear();
        m_highlightCacheDirty = false;
        return;
    }

    const QString query = searchQuery();
    if (query.isEmpty()
        || query.contains(QLatin1Char('\n'))
        || query.contains(QLatin1Char('\r'))) {
        m_cachedVisibleMatches.clear();
        m_highlightCacheDirty = false;
        return;
    }

    if (viewportMoving()) {
        if (!m_highlightRefreshTimer.isActive()) {
            m_highlightRefreshTimer.start();
        }
        return;
    }

    const int maxLine = qMax(0, totalLines() - 1);
    const int firstLine = qBound(0, m_firstVisibleLine - editorConfig().highlightCacheOverscanLines(), maxLine);
    const int lastVisible = m_firstVisibleLine + visibleLineCount() - 1;
    const int lastLine = qBound(0, lastVisible + editorConfig().highlightCacheOverscanLines(), maxLine);
    if (lastLine < firstLine) {
        m_cachedVisibleMatches.clear();
        m_highlightCacheDirty = false;
        return;
    }

    const int visibleStart = doc->lineStartOffset(firstLine);
    const int lastLineStart = doc->lineStartOffset(lastLine);
    const int lastLineLength = lineLengthAt(lastLine);
    const int visibleEnd = qMax(visibleStart, lastLineStart + lastLineLength);

    if (visibleEnd <= visibleStart) {
        m_cachedVisibleMatches.clear();
    } else {
        int fetchLimit = editorConfig().visibleMatchFetchLimit();
        if (query.length() <= 1) {
            fetchLimit = qMin(fetchLimit, editorConfig().visibleMatchFetchLimitScrolling());
        }
        m_cachedVisibleMatches = doc->searchMatchPositionsInRange(visibleStart, visibleEnd, fetchLimit);
    }

    m_highlightCacheFirstLine = firstLine;
    m_highlightCacheLastLine = lastLine;
    m_highlightCacheTextRevision = textRevision();
    m_highlightCacheQuery = query;
    m_highlightCacheDirty = false;
}

void PaintedEditorItem::markViewportMoved()
{
    m_lastViewportMotionMs = QDateTime::currentMSecsSinceEpoch();
}

bool PaintedEditorItem::viewportMoving() const
{
    if (m_lastViewportMotionMs <= 0) {
        return false;
    }
    return (QDateTime::currentMSecsSinceEpoch() - m_lastViewportMotionMs) <= editorConfig().viewportMotionWindowMs();
}

bool PaintedEditorItem::hasSelection() const
{
    return m_selectionAnchorOffset >= 0
        && m_selectionCursorOffset >= 0
        && m_selectionAnchorOffset != m_selectionCursorOffset;
}

int PaintedEditorItem::selectionStart() const
{
    return qMin(m_selectionAnchorOffset, m_selectionCursorOffset);
}

int PaintedEditorItem::selectionEnd() const
{
    return qMax(m_selectionAnchorOffset, m_selectionCursorOffset);
}

void PaintedEditorItem::clearSelectionToCursor()
{
    m_selectionAnchorOffset = m_cursorOffset;
    m_selectionCursorOffset = m_cursorOffset;
}

void PaintedEditorItem::ensureCursorVisible()
{
    if (totalLines() <= 0) {
        return;
    }
    if (m_cursorLine < m_firstVisibleLine) {
        setFirstVisibleLineInternal(m_cursorLine);
        return;
    }
    const int lines = visibleLineCount();
    if (m_cursorLine >= m_firstVisibleLine + lines) {
        setFirstVisibleLineInternal(m_cursorLine - lines + 1);
    }

    const qreal viewWidth = textViewportWidth();
    if (m_cursorXInLine < m_horizontalOffset) {
        setHorizontalOffsetInternal(m_cursorXInLine - 12.0);
    } else if (m_cursorXInLine > m_horizontalOffset + viewWidth - 8.0) {
        setHorizontalOffsetInternal(m_cursorXInLine - viewWidth + 20.0);
    }
}

void PaintedEditorItem::setFirstVisibleLineInternal(int line)
{
    const int clamped = qBound(0, line, maxFirstVisibleLine());
    if (m_firstVisibleLine == clamped) {
        return;
    }

    const bool wasMoving = viewportMoving();
    m_firstVisibleLine = clamped;
    markViewportMoved();
    pruneLineCache();
    invalidateHighlightCache(false);
    if (wasMoving) {
        m_horizontalMetricsTimer.start();
    } else {
        m_horizontalMetricsTimer.stop();
        updateHorizontalMetrics();
    }
    update();
    emit scrollMetricsChanged();
}

void PaintedEditorItem::syncCursorFromOffset()
{
    DocumentSession *doc = document();
    if (!doc || !occupied() || totalLines() <= 0) {
        m_cursorLine = 0;
        m_cursorColumn = 0;
        m_cursorXInLine = 0.0;
        return;
    }

    m_cursorOffset = clampedOffset(m_cursorOffset);
    m_cursorLine = qBound(0, doc->lineForOffsetZeroBased(m_cursorOffset), qMax(0, totalLines() - 1));
    const int start = doc->lineStartOffset(m_cursorLine);
    const int lineLength = lineLengthAt(m_cursorLine);
    m_cursorColumn = qBound(0, m_cursorOffset - start, lineLength);
    if (lineLength > editorConfig().longLineApproxThreshold()) {
        QFont font(QStringLiteral("Consolas"));
        font.setPixelSize(editorConfig().textPixelSize());
        QFontMetrics fm(font);
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        m_cursorXInLine = static_cast<qreal>(m_cursorColumn) * monoCharWidth;
    } else {
        const QString text = lineTextAt(m_cursorLine);
        m_cursorXInLine = xForColumn(text, m_cursorColumn);
    }
    ensureContentWidthForCursor();
}

void PaintedEditorItem::setCursorOffset(int offset, bool keepSelection, bool notifyController)
{
    DocumentSession *doc = document();
    if (!doc || !occupied()) {
        return;
    }

    const int clamped = clampedOffset(offset);
    const int oldCursor = m_cursorOffset;
    const int oldAnchor = m_selectionAnchorOffset;
    const int oldSelectionCursor = m_selectionCursorOffset;
    if (keepSelection) {
        if (m_selectionAnchorOffset < 0) {
            m_selectionAnchorOffset = m_cursorOffset;
        }
        m_selectionCursorOffset = clamped;
    } else {
        m_selectionAnchorOffset = clamped;
        m_selectionCursorOffset = clamped;
    }

    m_cursorOffset = clamped;
    const bool selectionChanged =
        (m_selectionAnchorOffset != oldAnchor) || (m_selectionCursorOffset != oldSelectionCursor);
    if (m_cursorOffset == oldCursor && !selectionChanged) {
        return;
    }
    m_preferredColumn = -1;
    syncCursorFromOffset();
    ensureCursorVisible();

    if (notifyController) {
        doc->setCursorPosition(m_cursorOffset);
    }

    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
}

bool PaintedEditorItem::ensureEditable()
{
    if (canEdit()) {
        return true;
    }
    emit toastRequested(QStringLiteral("Saving is in progress. Editing is disabled."));
    return false;
}

bool PaintedEditorItem::applyTextEdit(int position, int removeLength, const QString &insertedText)
{
    if (!ensureEditable()) {
        return false;
    }

    DocumentSession *doc = document();
    if (!doc) {
        return false;
    }

    const int newPos = doc->applyTextEdit(position, removeLength, insertedText);
    if (newPos < 0) {
        return false;
    }

    clearLineCache();
    m_cursorOffset = newPos;
    clearSelectionToCursor();
    syncCursorFromOffset();
    ensureCursorVisible();
    m_horizontalMetricsTimer.start();
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
    return true;
}

bool PaintedEditorItem::deleteCurrentSelection()
{
    if (!hasSelection()) {
        return true;
    }
    return applyTextEdit(selectionStart(), selectionEnd() - selectionStart(), QString());
}

void PaintedEditorItem::insertText(const QString &text)
{
    if (!occupied()) {
        return;
    }

    if (hasSelection()) {
        if (!deleteCurrentSelection()) {
            return;
        }
    }

    applyTextEdit(m_cursorOffset, 0, text);
}

QString PaintedEditorItem::preferredLineBreak() const
{
    DocumentSession *doc = document();
    if (!doc || !occupied()) {
        return QStringLiteral("\n");
    }

    const int total = doc->textLength();
    if (total <= 0) {
        return QStringLiteral("\n");
    }

    const int sampleLength = qMin(total, 64 * 1024);
    const QString head = doc->textSlice(0, sampleLength);

    if (head.contains(QStringLiteral("\r\n"))) {
        return QStringLiteral("\r\n");
    }
    if (head.contains(QLatin1Char('\n'))) {
        return QStringLiteral("\n");
    }
    if (head.contains(QLatin1Char('\r'))) {
        return QStringLiteral("\r");
    }

    return QStringLiteral("\n");
}

int PaintedEditorItem::backspaceRemoveLength() const
{
    if (m_cursorOffset <= 0) {
        return 0;
    }

    DocumentSession *doc = document();
    if (!doc || !occupied() || m_cursorOffset < 2) {
        return 1;
    }

    const QString prevTwo = doc->textSlice(m_cursorOffset - 2, 2);
    if (prevTwo == QStringLiteral("\r\n")) {
        return 2;
    }

    return 1;
}

int PaintedEditorItem::deleteRemoveLength() const
{
    DocumentSession *doc = document();
    if (!doc || !occupied()) {
        return 0;
    }

    const int total = doc->textLength();
    if (m_cursorOffset < 0 || m_cursorOffset >= total) {
        return 0;
    }

    if (m_cursorOffset + 1 < total) {
        const QString nextTwo = doc->textSlice(m_cursorOffset, 2);
        if (nextTwo == QStringLiteral("\r\n")) {
            return 2;
        }
    }

    return 1;
}

void PaintedEditorItem::moveHorizontal(int step, bool keepSelection)
{
    setCursorOffset(m_cursorOffset + step, keepSelection, true);
}

void PaintedEditorItem::moveVertical(int step, bool keepSelection)
{
    if (!occupied() || totalLines() <= 0) {
        return;
    }

    if (m_preferredColumn < 0) {
        m_preferredColumn = m_cursorColumn;
    }

    const int targetLine = qBound(0, m_cursorLine + step, totalLines() - 1);
    setCursorOffset(offsetForLineColumn(targetLine, m_preferredColumn), keepSelection, true);
}

void PaintedEditorItem::moveToLineBoundary(bool toLineStart, bool keepSelection)
{
    if (!occupied() || totalLines() <= 0) {
        return;
    }

    const int lineLength = lineLengthAt(m_cursorLine);
    const int targetCol = toLineStart ? 0 : lineLength;
    setCursorOffset(offsetForLineColumn(m_cursorLine, targetCol), keepSelection, true);
}

void PaintedEditorItem::startLongPress(const QPointF &pos)
{
    m_pressPoint = pos;
    m_longPressTimer.start();
}

void PaintedEditorItem::stopLongPress()
{
    if (m_longPressTimer.isActive()) {
        m_longPressTimer.stop();
    }
}

void PaintedEditorItem::startAutoScroll(int direction)
{
    if (direction > 0) {
        m_autoScrollDirection = 1;
    } else if (direction < 0) {
        m_autoScrollDirection = -1;
    } else {
        m_autoScrollDirection = 0;
    }

    if (m_autoScrollTimerId == 0) {
        m_autoScrollTimerId = startTimer(editorConfig().autoScrollTickMs());
    }
}

void PaintedEditorItem::stopAutoScroll()
{
    if (m_autoScrollTimerId != 0) {
        killTimer(m_autoScrollTimerId);
        m_autoScrollTimerId = 0;
    }
    m_autoScrollDirection = 0;
}

qreal PaintedEditorItem::textViewportWidth() const
{
    return qMax<qreal>(1.0, width() - textAreaLeft() - 2.0);
}

qreal PaintedEditorItem::maxHorizontalOffset() const
{
    return qMax<qreal>(0.0, m_contentWidth - textViewportWidth());
}

void PaintedEditorItem::ensureContentWidthForCursor()
{
    if (!occupied()) {
        return;
    }

    const qreal minWidth = qMax<qreal>(textViewportWidth(), m_cursorXInLine + 84.0);
    if (minWidth <= m_contentWidth + 0.5) {
        return;
    }

    m_contentWidth = minWidth;
    emit scrollMetricsChanged();
}

void PaintedEditorItem::setHorizontalOffsetInternal(qreal offset)
{
    const qreal clamped = qBound<qreal>(0.0, offset, maxHorizontalOffset());
    if (qAbs(m_horizontalOffset - clamped) < 0.5) {
        return;
    }
    m_horizontalOffset = clamped;
    markViewportMoved();
    update();
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    emit scrollMetricsChanged();
}

void PaintedEditorItem::updateHorizontalMetrics()
{
    const qreal oldContentWidth = m_contentWidth;
    qreal measured = 0.0;

    if (occupied() && totalLines() > 0) {
        DocumentSession *doc = document();
        if (doc) {
            QFont font(QStringLiteral("Consolas"));
            font.setPixelSize(editorConfig().textPixelSize());
            QFontMetrics fm(font);
            const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));

            const int count = visibleLineCount();
            for (int i = 0; i < count; ++i) {
                const int lineIndex = m_firstVisibleLine + i;
                if (lineIndex < 0 || lineIndex >= totalLines()) {
                    break;
                }
                const int lineLength = lineLengthAt(lineIndex);
                if (lineLength > editorConfig().longLineApproxThreshold()) {
                    const qreal width = monoCharWidth * static_cast<qreal>(lineLength);
                    m_lineWidthCache.insert(lineIndex, width);
                    measured = qMax<qreal>(measured, width);
                } else {
                    const QString text = lineTextAt(lineIndex);
                    measured = qMax<qreal>(measured, lineWidthAt(lineIndex, text, fm));
                }
            }
        }

        measured = qMax<qreal>(measured, m_cursorXInLine + 20.0);
    }

    m_contentWidth = qMax<qreal>(textViewportWidth(), measured + 64.0);
    if (qAbs(oldContentWidth - m_contentWidth) >= 0.5) {
        emit scrollMetricsChanged();
    }

    setHorizontalOffsetInternal(m_horizontalOffset);
}

void PaintedEditorItem::requestHighlightRefreshTimerStart()
{
    if (QThread::currentThread() == thread()) {
        if (!m_highlightRefreshTimer.isActive()) {
            m_highlightRefreshTimer.start();
        }
        return;
    }

    bool expected = false;
    if (!m_highlightRefreshStartQueued.compare_exchange_strong(expected, true)) {
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        m_highlightRefreshStartQueued.store(false);
        if (!m_highlightRefreshTimer.isActive()) {
            m_highlightRefreshTimer.start();
        }
    }, Qt::QueuedConnection);
}

qreal PaintedEditorItem::lineWidthAt(int line, const QString &lineText, const QFontMetrics &fm) const
{
    const auto it = m_lineWidthCache.constFind(line);
    if (it != m_lineWidthCache.constEnd()) {
        return it.value();
    }

    qreal width = 0.0;
    if (lineText.length() > editorConfig().longLineApproxThreshold()) {
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        width = monoCharWidth * static_cast<qreal>(lineText.length());
    } else {
        width = static_cast<qreal>(fm.horizontalAdvance(lineText));
    }
    m_lineWidthCache.insert(line, width);
    return width;
}
