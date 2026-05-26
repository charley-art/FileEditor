#include "paintededitoritem.h"

#include "nc/ncdiagnosticmessages.h"
#include "nc/ncparser.h"
#include "workspacecontroller.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QStringList>
#include <QThread>
#include <QWheelEvent>
#include <QtConcurrent>

#include <algorithm>

namespace {
constexpr int kLineHeight = 34;
constexpr int kTextPixelSize = 20;
constexpr int kLongPressMs = 450;
constexpr int kAutoScrollTickMs = 30;
constexpr int kSelectionDragThreshold = 4;
constexpr int kMaxFindHighlightsPerLine = 512;
constexpr int kVisibleMatchFetchLimit = 1000;
constexpr int kVisibleMatchFetchLimitScrolling = 320;
constexpr int kLongLineApproxThreshold = 4096;
constexpr int kPaintOverscanColumns = 24;
constexpr int kLongLineHighlightOverscanColumns = 48;
constexpr int kLineCacheMarginLines = 256;
constexpr int kHighlightRefreshMs = 70;
constexpr int kDiagnosticRefreshMs = 120;
constexpr int kDiagnosticContextLines = 200;
constexpr int kHighlightCacheOverscanLines = 12;
constexpr int kHighlightFrameBudgetIdle = 720;
constexpr int kHighlightFrameBudgetScrolling = 200;
constexpr int kViewportMotionWindowMs = 140;
constexpr int kHorizontalMetricsRefreshMs = 80;
constexpr auto kInputMethodUpdateQueries =
    Qt::ImCursorRectangle | Qt::ImSurroundingText | Qt::ImCurrentSelection;

int diagnosticRank(int severity)
{
    return severity;
}

QColor diagnosticColor(int severity)
{
    if (severity >= static_cast<int>(nc::DiagnosticSeverity::Error)) {
        return QColor(QStringLiteral("#ff5d66"));
    }
    if (severity >= static_cast<int>(nc::DiagnosticSeverity::Warning)) {
        return QColor(QStringLiteral("#f5c469"));
    }
    return QColor(QStringLiteral("#7db7ff"));
}

QString diagnosticSeverityLabel(int severity)
{
    if (severity >= static_cast<int>(nc::DiagnosticSeverity::Error)) {
        return QStringLiteral("\u9519\u8bef");
    }
    if (severity >= static_cast<int>(nc::DiagnosticSeverity::Warning)) {
        return QStringLiteral("\u8b66\u544a");
    }
    return QStringLiteral("\u63d0\u793a");
}

QString diagnosticSummaryLine(int severity, const QString &message)
{
    return QStringLiteral("%1: %2").arg(diagnosticSeverityLabel(severity), message);
}
}

PaintedEditorItem::PaintedEditorItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
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
    m_longPressTimer.setInterval(kLongPressMs);
    connect(&m_longPressTimer, &QTimer::timeout, this, [this]() {
        if (!m_mousePressed || m_dragStarted) {
            return;
        }
        emit menuRequested(m_pressPoint.x(), m_pressPoint.y());
    });

    m_highlightRefreshTimer.setSingleShot(true);
    m_highlightRefreshTimer.setInterval(kHighlightRefreshMs);
    connect(&m_highlightRefreshTimer, &QTimer::timeout, this, [this]() {
        if (viewportMoving()) {
            m_highlightRefreshTimer.start();
            return;
        }
        refreshVisibleMatchCache();
        update();
    });

    m_diagnosticRefreshTimer.setSingleShot(true);
    m_diagnosticRefreshTimer.setInterval(kDiagnosticRefreshMs);
    connect(&m_diagnosticRefreshTimer, &QTimer::timeout, this, [this]() {
        if (viewportMoving()) {
            m_diagnosticRefreshTimer.start();
            return;
        }
        refreshVisibleDiagnostics();
        update();
    });

    m_horizontalMetricsTimer.setSingleShot(true);
    m_horizontalMetricsTimer.setInterval(kHorizontalMetricsRefreshMs);
    connect(&m_horizontalMetricsTimer, &QTimer::timeout, this, [this]() {
        updateHorizontalMetrics();
    });

    m_perfClock.start();
    m_perfWindowStartMs = 0;
    m_perfFrameCount = 0;
    m_perfPublishTimer.setSingleShot(true);
    m_perfPublishTimer.setInterval(180);
    connect(&m_perfPublishTimer, &QTimer::timeout, this, [this]() {
        emit perfStatsChanged();
    });
}

void PaintedEditorItem::paint(QPainter *painter)
{
    const bool trackPerf = m_perfStatsEnabled;
    QElapsedTimer paintTimer;
    if (trackPerf) {
        paintTimer.start();
    }
    const auto finalizePaintStats = [this, &paintTimer, trackPerf]() {
        if (!trackPerf) {
            return;
        }
        const qreal elapsedMs = qMax<qreal>(0.0,
                                            static_cast<qreal>(paintTimer.nsecsElapsed()) / 1000000.0);
        m_lastPaintMs = elapsedMs;
        if (m_averagePaintMs <= 0.0) {
            m_averagePaintMs = elapsedMs;
        } else {
            m_averagePaintMs = m_averagePaintMs * 0.9 + elapsedMs * 0.1;
        }

        if (!m_perfClock.isValid()) {
            m_perfClock.start();
            m_perfWindowStartMs = 0;
            m_perfFrameCount = 0;
        }

        ++m_perfFrameCount;
        const qint64 nowMs = m_perfClock.elapsed();
        if (m_perfWindowStartMs <= 0) {
            m_perfWindowStartMs = nowMs;
        } else {
            const qint64 spanMs = nowMs - m_perfWindowStartMs;
            if (spanMs >= 500) {
                m_paintFps = qMax(0, qRound(static_cast<qreal>(m_perfFrameCount) * 1000.0
                                            / static_cast<qreal>(spanMs)));
                m_perfFrameCount = 0;
                m_perfWindowStartMs = nowMs;
            }
        }

        const int visibleMatches = m_cachedVisibleMatches.size();
        if (m_visibleMatchCacheSize != visibleMatches) {
            m_visibleMatchCacheSize = visibleMatches;
        }
        queuePerfStatsPublish();
    };

    painter->fillRect(boundingRect(), QColor(QStringLiteral("#0a1528")));

    if (!m_occupied || m_totalLines <= 0) {
        finalizePaintStats();
        return;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
        finalizePaintStats();
        return;
    }

    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(kTextPixelSize);
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
    const bool hasQuery = !m_searchQuery.isEmpty();
    const bool supportsInlineHighlight =
        hasQuery && !m_searchQuery.contains(QLatin1Char('\n')) && !m_searchQuery.contains(QLatin1Char('\r'));
    const int queryLen = m_searchQuery.length();
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
    int frameHighlightBudget = moving ? kHighlightFrameBudgetScrolling : kHighlightFrameBudgetIdle;
    if (queryLen <= 1) {
        frameHighlightBudget = qMin(frameHighlightBudget, moving ? 120 : 360);
    }
    if (m_diagnosticCacheTextRevision != m_textRevision
        || m_diagnosticCacheFirstLine != m_firstVisibleLine
        || m_diagnosticCacheVisibleLineCount != lineCount) {
        m_diagnosticCacheDirty = true;
    }
    if (m_diagnosticCacheDirty && !m_diagnosticWatcher) {
        requestDiagnosticRefreshTimerStart();
    }

    for (int i = 0; i < lineCount; ++i) {
        const int lineIndex = m_firstVisibleLine + i;
        if (lineIndex < 0 || lineIndex >= m_totalLines) {
            break;
        }

        const int y = i * kLineHeight;
        QRect lineRect(0, y, static_cast<int>(width()), kLineHeight);

        if (lineIndex == m_cursorLine) {
            painter->fillRect(lineRect, QColor(QStringLiteral("#163b63")));
        } else if (lineIndex + 1 == m_currentLine) {
            painter->fillRect(lineRect, QColor(QStringLiteral("#102b48")));
        }

        painter->setPen(QColor(QStringLiteral("#7087ad")));
        painter->drawText(QRect(8, y, numberWidth - 12, kLineHeight),
                          Qt::AlignRight | Qt::AlignVCenter,
                          QString::number(lineIndex + 1));
        const QVector<int> lineDiagnosticIndexes = diagnosticIndexesForLine(lineIndex);
        if (!lineDiagnosticIndexes.isEmpty()) {
            int severity = 0;
            for (const int markerIndex : lineDiagnosticIndexes) {
                severity = qMax(severity, m_visibleDiagnostics.at(markerIndex).severity);
            }
            painter->fillRect(QRectF(numberWidth - 5, y + 8, 3, kLineHeight - 16),
                              diagnosticColor(severity));
        }

        painter->save();
        painter->setClipRect(QRectF(textBaseLeft, y, qMax<qreal>(1.0, width() - textBaseLeft), kLineHeight));

        const int lineStart = ctrl->lineStartOffset(m_slotIndex, lineIndex);
        const int lineLength = lineLengthAt(lineIndex);
        const bool useApproxLine = lineLength > kLongLineApproxThreshold;
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
                                             - kLongLineHighlightOverscanColumns,
                                         lineLength);
                const int visibleCols = qMax(1,
                                             static_cast<int>(viewportWidth / monoCharWidth)
                                                 + 2 * kLongLineHighlightOverscanColumns);
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
                const QRectF matchRect(sx, y + 7, qMax<qreal>(2.0, ex - sx), kLineHeight - 14);
                painter->fillRect(matchRect, QColor(QStringLiteral("#2c5d3e")));

                ++highlighted;
                --frameHighlightBudget;
                if (highlighted >= kMaxFindHighlightsPerLine) {
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
                QRectF highlightRect(sx, y + 4, qMax<qreal>(2.0, ex - sx), kLineHeight - 8);
                painter->fillRect(highlightRect, QColor(QStringLiteral("#225c9b")));
            }
        }

        painter->setPen(QColor(QStringLiteral("#eaf1ff")));
        const int baseline = y + (kLineHeight + fm.ascent() - fm.descent()) / 2;
        QString drawText = text;
        qreal drawX = textLeft;
        if (useApproxLine && lineLength > 0) {
            const int startCol = qBound(0,
                                        static_cast<int>(m_horizontalOffset / monoCharWidth) - kPaintOverscanColumns,
                                        lineLength);
            const int visibleCols = qMax(1,
                                         static_cast<int>(viewportWidth / monoCharWidth)
                                             + 2 * kPaintOverscanColumns);
            const int endCol = qBound(startCol, startCol + visibleCols, lineLength);
            drawText = ctrl->lineTextSlice(m_slotIndex, lineIndex, startCol, endCol - startCol);
            drawX = textLeft + static_cast<qreal>(startCol) * monoCharWidth;
        }
        painter->drawText(QPointF(drawX, baseline), drawText);

        if (!lineDiagnosticIndexes.isEmpty() && lineLength > 0) {
            for (const int markerIndex : lineDiagnosticIndexes) {
                const DiagnosticMarker &marker = m_visibleDiagnostics.at(markerIndex);
                const bool lineLevelMarker = marker.column <= 1 && marker.length >= lineLength;
                if (lineLevelMarker) {
                    continue;
                }
                const int startCol = qBound(0, marker.column - 1, lineLength);
                const int endCol = qBound(startCol + 1, startCol + marker.length, lineLength);
                const qreal sx = textLeft + xForColFast(startCol);
                const qreal ex = textLeft + xForColFast(endCol);
                if (ex < textBaseLeft || sx > width()) {
                    continue;
                }

                const QColor color = diagnosticColor(marker.severity);
                if (!useApproxLine && endCol > startCol) {
                    painter->setPen(color);
                    painter->drawText(QPointF(sx, baseline), text.mid(startCol, endCol - startCol));
                }

                QPen underlinePen(color, 2.0);
                underlinePen.setCapStyle(Qt::RoundCap);
                painter->setPen(underlinePen);
                const qreal underlineY = y + kLineHeight - 7;
                painter->drawLine(QPointF(sx, underlineY),
                                  QPointF(qMax<qreal>(sx + 4.0, ex), underlineY));
            }
        }

        if (hasActiveFocus() && !m_preeditText.isEmpty() && lineIndex == m_cursorLine) {
            const qreal preeditX = textLeft + m_cursorXInLine;
            const qreal preeditWidth = static_cast<qreal>(fm.horizontalAdvance(m_preeditText));
            painter->setPen(QColor(QStringLiteral("#f5d18f")));
            painter->drawText(QPointF(preeditX, baseline), m_preeditText);
            painter->drawLine(QPointF(preeditX, y + kLineHeight - 7),
                              QPointF(preeditX + preeditWidth, y + kLineHeight - 7));
        }
        painter->restore();
    }

    if (hasActiveFocus()) {
        const int drawLine = m_cursorLine - m_firstVisibleLine;
        if (drawLine >= 0 && drawLine < lineCount) {
            const qreal cx = textLeft + m_cursorXInLine + preeditCursorDx;
            const int cy = drawLine * kLineHeight;
            painter->save();
            painter->setClipRect(QRectF(textBaseLeft, cy, qMax<qreal>(1.0, width() - textBaseLeft), kLineHeight));
            painter->fillRect(QRectF(cx, cy + 5, 1, kLineHeight - 10), QColor(QStringLiteral("#f0f6ff")));
            painter->restore();
        }
    }

    const int primaryDiagnosticIndex = hasActiveFocus() ? primaryDiagnosticIndexForLine(m_cursorLine) : -1;
    if (primaryDiagnosticIndex >= 0) {
        const int drawLine = m_cursorLine - m_firstVisibleLine;
        if (drawLine >= 0 && drawLine < lineCount) {
            QVector<int> sameLineDiagnostics = diagnosticIndexesForLine(m_cursorLine);
            std::sort(sameLineDiagnostics.begin(), sameLineDiagnostics.end(), [this](int left, int right) {
                const DiagnosticMarker &a = m_visibleDiagnostics.at(left);
                const DiagnosticMarker &b = m_visibleDiagnostics.at(right);
                if (diagnosticRank(a.severity) != diagnosticRank(b.severity)) {
                    return diagnosticRank(a.severity) > diagnosticRank(b.severity);
                }
                return a.column < b.column;
            });
            const DiagnosticMarker &marker = m_visibleDiagnostics.at(sameLineDiagnostics.first());
            const int displayCount = qMin(3, sameLineDiagnostics.size());
            const int remainingCount = qMax(0, sameLineDiagnostics.size() - displayCount);

            QFont popupFont(QStringLiteral("Microsoft YaHei UI"));
            popupFont.setPixelSize(14);
            painter->setFont(popupFont);
            QFontMetrics popupFm(popupFont);

            const qreal margin = 10.0;
            const qreal maxPopupWidth = qMax<qreal>(180.0, width() - textAreaLeft() - 24.0);
            QStringList linesToDraw;
            qreal popupTextWidth = 0.0;
            for (int i = 0; i < displayCount; ++i) {
                const DiagnosticMarker &lineMarker = m_visibleDiagnostics.at(sameLineDiagnostics.at(i));
                const QString line = diagnosticSummaryLine(lineMarker.severity, lineMarker.message);
                const QString elidedLine =
                    popupFm.elidedText(line, Qt::ElideRight, static_cast<int>(maxPopupWidth - 2 * margin - 10.0));
                linesToDraw.push_back(elidedLine);
                popupTextWidth = qMax<qreal>(popupTextWidth, popupFm.horizontalAdvance(elidedLine));
            }
            if (remainingCount > 0) {
                const QString moreLine = QStringLiteral("\u53e6\u6709 %1 \u4e2a\u95ee\u9898").arg(remainingCount);
                linesToDraw.push_back(moreLine);
                popupTextWidth = qMax<qreal>(popupTextWidth, popupFm.horizontalAdvance(moreLine));
            }

            const qreal popupWidth = qMin<qreal>(maxPopupWidth, popupTextWidth + 2 * margin + 10.0);
            const qreal lineStep = qMax<qreal>(18.0, popupFm.height() + 2.0);
            const qreal popupHeight = qMax<qreal>(32.0, linesToDraw.size() * lineStep + 12.0);
            const int cursorLineLength = lineLengthAt(m_cursorLine);
            const int popupColumn = qBound(0, marker.column - 1, cursorLineLength);
            qreal popupColumnX = 0.0;
            if (cursorLineLength > kLongLineApproxThreshold) {
                popupColumnX = static_cast<qreal>(popupColumn) * monoCharWidth;
            } else {
                popupColumnX = xForColumn(lineTextAt(m_cursorLine), popupColumn);
            }
            qreal popupX = qBound<qreal>(textAreaLeft(),
                                         textLeft + popupColumnX,
                                         qMax<qreal>(textAreaLeft(), width() - popupWidth - 8.0));
            qreal popupY = drawLine * kLineHeight - popupHeight - 4.0;
            if (popupY < 4.0) {
                popupY = drawLine * kLineHeight + kLineHeight + 4.0;
            }
            popupY = qBound<qreal>(4.0, popupY, qMax<qreal>(4.0, height() - popupHeight - 4.0));

            const QRectF popupRect(popupX, popupY, popupWidth, popupHeight);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(diagnosticColor(marker.severity), 1.0));
            painter->setBrush(QColor(QStringLiteral("#23131a")));
            painter->drawRoundedRect(popupRect, 5.0, 5.0);
            for (int i = 0; i < linesToDraw.size(); ++i) {
                QColor textColor(QStringLiteral("#ffecef"));
                if (i < displayCount) {
                    const DiagnosticMarker &lineMarker = m_visibleDiagnostics.at(sameLineDiagnostics.at(i));
                    textColor = diagnosticColor(lineMarker.severity);
                }
                painter->setPen(textColor);
                const QRectF lineRect(popupRect.left() + margin,
                                      popupRect.top() + 6.0 + static_cast<qreal>(i) * lineStep,
                                      popupRect.width() - 2 * margin,
                                      lineStep);
                painter->drawText(lineRect, Qt::AlignVCenter | Qt::AlignLeft, linesToDraw.at(i));
            }
            painter->restore();

            painter->setFont(font);
        }
    }

    finalizePaintStats();
}

QObject *PaintedEditorItem::controller() const
{
    return m_controller;
}

void PaintedEditorItem::setController(QObject *controller)
{
    if (m_controller == controller) {
        return;
    }
    m_controller = controller;
    m_horizontalMetricsTimer.stop();
    m_lastViewportMotionMs = 0;
    clearLineCache();
    invalidateHighlightCache(true);
    invalidateDiagnosticCache(true);
    syncCursorFromOffset();
    updateHorizontalMetrics();
    update();
    emit controllerChanged();
    emit scrollMetricsChanged();
}

int PaintedEditorItem::slotIndex() const
{
    return m_slotIndex;
}

void PaintedEditorItem::setSlotIndex(int slotIndex)
{
    if (m_slotIndex == slotIndex) {
        return;
    }
    m_slotIndex = slotIndex;
    m_firstVisibleLine = 0;
    m_cursorOffset = 0;
    m_selectionAnchorOffset = -1;
    m_selectionCursorOffset = -1;
    m_horizontalOffset = 0.0;
    m_contentWidth = 0.0;
    m_preeditText.clear();
    m_preeditCursorInString = -1;
    m_horizontalMetricsTimer.stop();
    m_lastViewportMotionMs = 0;
    clearLineCache();
    invalidateHighlightCache(true);
    invalidateDiagnosticCache(true);
    syncCursorFromOffset();
    updateHorizontalMetrics();
    update();
    emit slotIndexChanged();
    emit scrollMetricsChanged();
}

bool PaintedEditorItem::occupied() const
{
    return m_occupied;
}

void PaintedEditorItem::setOccupied(bool occupied)
{
    if (m_occupied == occupied) {
        return;
    }
    m_occupied = occupied;
    if (!m_occupied) {
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
        invalidateHighlightCache(true);
        invalidateDiagnosticCache(true);
    } else {
        m_selectionAnchorOffset = m_cursorOffset;
        m_selectionCursorOffset = m_cursorOffset;
        invalidateHighlightCache(true);
        invalidateDiagnosticCache(true);
        syncCursorFromOffset();
        updateHorizontalMetrics();
    }
    clearLineCache();
    update();
    emit occupiedChanged();
    emit scrollMetricsChanged();
}

bool PaintedEditorItem::canEdit() const
{
    return m_canEdit;
}

void PaintedEditorItem::setCanEdit(bool canEdit)
{
    if (m_canEdit == canEdit) {
        return;
    }
    m_canEdit = canEdit;
    emit canEditChanged();
}

int PaintedEditorItem::totalLines() const
{
    return m_totalLines;
}

void PaintedEditorItem::setTotalLines(int totalLines)
{
    totalLines = qMax(0, totalLines);
    if (m_totalLines == totalLines) {
        return;
    }
    m_totalLines = totalLines;
    setFirstVisibleLineInternal(m_firstVisibleLine);
    invalidateHighlightCache(false);
    invalidateDiagnosticCache(true);
    syncCursorFromOffset();
    m_horizontalMetricsTimer.start();
    update();
    emit totalLinesChanged();
    emit scrollMetricsChanged();
}

int PaintedEditorItem::currentLine() const
{
    return m_currentLine;
}

void PaintedEditorItem::setCurrentLine(int currentLine)
{
    if (m_currentLine == currentLine) {
        return;
    }
    m_currentLine = currentLine;
    update();
    emit currentLineChanged();
}

int PaintedEditorItem::textRevision() const
{
    return m_textRevision;
}

void PaintedEditorItem::setTextRevision(int textRevision)
{
    if (m_textRevision == textRevision) {
        return;
    }
    m_textRevision = textRevision;
    clearLineCache();
    invalidateHighlightCache(false);
    invalidateDiagnosticCache(true);
    syncCursorFromOffset();
    m_horizontalMetricsTimer.start();
    update();
    emit textRevisionChanged();
}

bool PaintedEditorItem::multiSelectEnabled() const
{
    return m_multiSelectEnabled;
}

void PaintedEditorItem::setMultiSelectEnabled(bool enabled)
{
    if (m_multiSelectEnabled == enabled) {
        return;
    }
    m_multiSelectEnabled = enabled;
    emit multiSelectEnabledChanged();
}

QString PaintedEditorItem::searchQuery() const
{
    return m_searchQuery;
}

void PaintedEditorItem::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query) {
        return;
    }
    m_searchQuery = query;
    invalidateHighlightCache(true);
    update();
    emit searchQueryChanged();
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
    if (m_totalLines <= 0) {
        return 1.0;
    }
    const qreal ratio = static_cast<qreal>(visibleLineCount()) / static_cast<qreal>(m_totalLines);
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

bool PaintedEditorItem::perfStatsEnabled() const
{
    return m_perfStatsEnabled;
}

void PaintedEditorItem::setPerfStatsEnabled(bool enabled)
{
    if (m_perfStatsEnabled == enabled) {
        return;
    }
    m_perfStatsEnabled = enabled;
    if (!m_perfStatsEnabled) {
        m_perfPublishTimer.stop();
        m_lastPaintMs = 0.0;
        m_averagePaintMs = 0.0;
        m_paintFps = 0;
        m_visibleMatchCacheSize = 0;
        m_perfFrameCount = 0;
        m_perfWindowStartMs = 0;
        m_perfClock.restart();
    } else if (!m_perfClock.isValid()) {
        m_perfClock.start();
    }

    emit perfStatsEnabledChanged();
    emit perfStatsChanged();
}

qreal PaintedEditorItem::lastPaintMs() const
{
    return m_lastPaintMs;
}

qreal PaintedEditorItem::averagePaintMs() const
{
    return m_averagePaintMs;
}

int PaintedEditorItem::paintFps() const
{
    return m_paintFps;
}

int PaintedEditorItem::visibleMatchCacheSize() const
{
    return m_visibleMatchCacheSize;
}

void PaintedEditorItem::selectByOffset(int offset)
{
    if (!m_occupied) {
        return;
    }
    forceActiveFocus();
    setCursorOffset(offset, false, true);
}

void PaintedEditorItem::selectRange(int start, int length)
{
    if (!m_occupied) {
        return;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
        return;
    }

    const int total = ctrl->textLength(m_slotIndex);
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
    ctrl->updateCursorPosition(m_slotIndex, m_cursorOffset);
    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
}

void PaintedEditorItem::performCopy()
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied) {
        return;
    }

    if (hasSelection()) {
        ctrl->setClipboardText(ctrl->textSlice(m_slotIndex, selectionStart(), selectionEnd() - selectionStart()));
        return;
    }

    ctrl->setClipboardText(lineTextAt(m_cursorLine));
}

void PaintedEditorItem::performCut()
{
    if (!m_occupied || !ensureEditable()) {
        return;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
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

    ctrl->setClipboardText(line);
    if (ctrl->deleteLineAt(m_slotIndex, m_cursorLine)) {
        clearLineCache();
        m_cursorOffset = clampedOffset(m_cursorOffset);
        clearSelectionToCursor();
        syncCursorFromOffset();
        ctrl->updateCursorPosition(m_slotIndex, m_cursorOffset);
        update();
    }
}

void PaintedEditorItem::performPaste()
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || !ensureEditable()) {
        return;
    }

    const QString clip = ctrl->clipboardText();
    if (!ctrl->canPaste(clip)) {
        const int kb = qMax(1, ctrl->pasteLimitBytes() / 1024);
        emit toastRequested(QStringLiteral("\u5355\u6b21\u7c98\u8d34\u8d85\u8fc7\u4e0a\u9650\uff08%1KB\uff09\uff0c\u5df2\u62e6\u622a\u3002").arg(kb));
        return;
    }

    insertText(clip);
}

void PaintedEditorItem::performDelete()
{
    if (!m_occupied || !ensureEditable()) {
        return;
    }

    if (hasSelection()) {
        deleteCurrentSelection();
        return;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
        return;
    }

    const int totalLength = ctrl->textLength(m_slotIndex);
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
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied) {
        return;
    }

    if (!ensureEditable()) {
        return;
    }

    if (!ctrl->undoEdit(m_slotIndex)) {
        emit toastRequested(QStringLiteral("\u6ca1\u6709\u53ef\u64a4\u9500\u7684\u64cd\u4f5c\u3002"));
        return;
    }

    clearLineCache();
    invalidateDiagnosticCache(true);
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
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied) {
        return;
    }

    if (!ensureEditable()) {
        return;
    }

    if (!ctrl->redoEdit(m_slotIndex)) {
        emit toastRequested(QStringLiteral("\u6ca1\u6709\u53ef\u6062\u590d\u7684\u64cd\u4f5c\u3002"));
        return;
    }

    clearLineCache();
    invalidateDiagnosticCache(true);
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
    invalidateDiagnosticCache(false);
    updateHorizontalMetrics();
    emit scrollMetricsChanged();
}

void PaintedEditorItem::mousePressEvent(QMouseEvent *event)
{
    if (!m_occupied || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }

    forceActiveFocus();
    emit focusRequested(m_slotIndex);

    m_mousePressed = true;
    m_dragStarted = false;
    m_pressInsideSelection = false;
    m_pressPoint = event->pos();
    m_lastMousePoint = event->pos();
    m_pressOffset = offsetForPoint(event->pos());

    const bool keepSelection = (event->modifiers() & Qt::ShiftModifier);
    if (m_multiSelectEnabled && hasSelection() && !keepSelection) {
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
    if (!m_mousePressed || !m_occupied) {
        event->ignore();
        return;
    }

    m_lastMousePoint = event->pos();
    if ((event->pos() - m_pressPoint).manhattanLength() >= kSelectionDragThreshold) {
        m_dragStarted = true;
        stopLongPress();
    }

    if (m_multiSelectEnabled) {
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
    if (!m_occupied) {
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
    if (!m_occupied) {
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
        WorkspaceController *ctrlPtr = workspaceController();
        if (ctrlPtr) {
            m_selectionAnchorOffset = 0;
            m_selectionCursorOffset = ctrlPtr->textLength(m_slotIndex);
            m_cursorOffset = m_selectionCursorOffset;
            syncCursorFromOffset();
            ctrlPtr->updateCursorPosition(m_slotIndex, m_cursorOffset);
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
        emit findRequested(m_slotIndex);
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
    if (!m_occupied) {
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
    WorkspaceController *ctrl = workspaceController();

    switch (query) {
    case Qt::ImEnabled:
        return m_occupied && m_canEdit;
    case Qt::ImCursorRectangle: {
        const int drawLine = m_cursorLine - m_firstVisibleLine;
        qreal preeditDx = 0.0;
        if (!m_preeditText.isEmpty()) {
            QFont font(QStringLiteral("Consolas"));
            font.setPixelSize(kTextPixelSize);
            QFontMetrics fm(font);
            const int cursor = qBound(0,
                                      m_preeditCursorInString < 0 ? m_preeditText.length() : m_preeditCursorInString,
                                      m_preeditText.length());
            preeditDx = static_cast<qreal>(fm.horizontalAdvance(m_preeditText.left(cursor)));
        }
        QRectF rect(textAreaLeft() + m_cursorXInLine + preeditDx - m_horizontalOffset,
                    drawLine * kLineHeight,
                    1,
                    kLineHeight);
        return mapRectToScene(rect);
    }
    case Qt::ImCursorPosition:
    case Qt::ImAbsolutePosition:
        return m_cursorOffset;
    case Qt::ImAnchorPosition:
        return hasSelection() ? selectionStart() : m_cursorOffset;
    case Qt::ImCurrentSelection:
        if (!ctrl || !hasSelection()) {
            return QString();
        }
        return ctrl->textSlice(m_slotIndex,
                               selectionStart(),
                               qMin(4096, selectionEnd() - selectionStart()));
    case Qt::ImSurroundingText:
        if (!ctrl) {
            return QString();
        }
        return ctrl->textSlice(m_slotIndex,
                               qMax(0, m_cursorOffset - 2048),
                               qMin(4096, ctrl->textLength(m_slotIndex) - qMax(0, m_cursorOffset - 2048)));
    case Qt::ImTextBeforeCursor:
        if (!ctrl) {
            return QString();
        }
        return ctrl->textSlice(m_slotIndex, qMax(0, m_cursorOffset - 1024), qMin(1024, m_cursorOffset));
    case Qt::ImTextAfterCursor:
        if (!ctrl) {
            return QString();
        }
        return ctrl->textSlice(m_slotIndex,
                               m_cursorOffset,
                               qMin(1024, ctrl->textLength(m_slotIndex) - m_cursorOffset));
    default:
        return {};
    }
}

void PaintedEditorItem::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_autoScrollTimerId) {
        if (!m_mousePressed || !m_multiSelectEnabled) {
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

WorkspaceController *PaintedEditorItem::workspaceController() const
{
    return qobject_cast<WorkspaceController *>(m_controller);
}

int PaintedEditorItem::visibleLineCount() const
{
    return qMax(1, static_cast<int>(height()) / kLineHeight + 1);
}

int PaintedEditorItem::maxFirstVisibleLine() const
{
    return qMax(0, m_totalLines - visibleLineCount());
}

int PaintedEditorItem::clampedOffset(int offset) const
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
        return qMax(0, offset);
    }
    const int totalLength = ctrl->textLength(m_slotIndex);
    return qBound(0, offset, totalLength);
}

int PaintedEditorItem::lineNumberWidth() const
{
    const int digits = QString::number(qMax(1, m_totalLines)).size();
    if (digits != m_cachedLineNumberDigits) {
        QFont font(QStringLiteral("Consolas"));
        font.setPixelSize(kTextPixelSize - 2);
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
    if (m_totalLines <= 0) {
        return 0;
    }
    const int local = qBound(0, static_cast<int>(y) / kLineHeight, visibleLineCount() - 1);
    return qBound(0, m_firstVisibleLine + local, m_totalLines - 1);
}

int PaintedEditorItem::offsetForPoint(const QPointF &point) const
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || m_totalLines <= 0) {
        return 0;
    }

    const int line = lineAtY(point.y());
    const qreal xInText = point.x() - textAreaLeft() + m_horizontalOffset;
    const int lineLength = lineLengthAt(line);

    int column = 0;
    if (lineLength > kLongLineApproxThreshold) {
        QFont font(QStringLiteral("Consolas"));
        font.setPixelSize(kTextPixelSize);
        QFontMetrics fm(font);
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        column = qBound(0, static_cast<int>(xInText / monoCharWidth), lineLength);
    } else {
        const QString text = lineTextAt(line);
        column = columnForX(text, xInText);
    }

    const int lineStart = ctrl->lineStartOffset(m_slotIndex, line);
    return lineStart + column;
}

int PaintedEditorItem::offsetForLineColumn(int line, int column) const
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || m_totalLines <= 0) {
        return 0;
    }
    const int safeLine = qBound(0, line, m_totalLines - 1);
    const int lineLength = lineLengthAt(safeLine);
    const int safeCol = qBound(0, column, lineLength);
    return ctrl->lineStartOffset(m_slotIndex, safeLine) + safeCol;
}

int PaintedEditorItem::columnForX(const QString &lineText, qreal xInText) const
{
    if (xInText <= 0.0 || lineText.isEmpty()) {
        return 0;
    }

    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(kTextPixelSize);
    QFontMetrics fm(font);
    if (lineText.length() > kLongLineApproxThreshold) {
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
    font.setPixelSize(kTextPixelSize);
    QFontMetrics fm(font);
    const int safeColumn = qBound(0, column, lineText.length());
    if (lineText.length() > kLongLineApproxThreshold) {
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        return static_cast<qreal>(safeColumn) * monoCharWidth;
    }
    return static_cast<qreal>(fm.horizontalAdvance(lineText.left(safeColumn)));
}

QString PaintedEditorItem::lineTextAt(int line) const
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || line < 0 || line >= m_totalLines) {
        return {};
    }

    const auto it = m_lineCache.constFind(line);
    if (it != m_lineCache.constEnd()) {
        return it.value();
    }

    const QString text = ctrl->lineText(m_slotIndex, line);
    m_lineCache.insert(line, text);
    return text;
}

int PaintedEditorItem::lineLengthAt(int line) const
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || line < 0 || line >= m_totalLines) {
        return 0;
    }

    const auto it = m_lineLengthCache.constFind(line);
    if (it != m_lineLengthCache.constEnd()) {
        return it.value();
    }

    const int length = qMax(0, ctrl->lineLength(m_slotIndex, line));
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
    if (!m_occupied || m_totalLines <= 0) {
        clearLineCache();
        return;
    }

    const int visible = visibleLineCount();
    const int keepStart = qMax(0, m_firstVisibleLine - kLineCacheMarginLines);
    const int keepEnd = qMin(m_totalLines - 1, m_firstVisibleLine + visible + kLineCacheMarginLines);

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
        m_visibleMatchCacheSize = 0;
        queuePerfStatsPublish();
    }

    if (!m_occupied || m_totalLines <= 0 || m_searchQuery.isEmpty()) {
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

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || m_totalLines <= 0) {
        m_cachedVisibleMatches.clear();
        m_visibleMatchCacheSize = 0;
        m_highlightCacheDirty = false;
        queuePerfStatsPublish();
        return;
    }

    if (m_searchQuery.isEmpty()
        || m_searchQuery.contains(QLatin1Char('\n'))
        || m_searchQuery.contains(QLatin1Char('\r'))) {
        m_cachedVisibleMatches.clear();
        m_visibleMatchCacheSize = 0;
        m_highlightCacheDirty = false;
        queuePerfStatsPublish();
        return;
    }

    if (viewportMoving()) {
        if (!m_highlightRefreshTimer.isActive()) {
            m_highlightRefreshTimer.start();
        }
        return;
    }

    const int maxLine = qMax(0, m_totalLines - 1);
    const int firstLine = qBound(0, m_firstVisibleLine - kHighlightCacheOverscanLines, maxLine);
    const int lastVisible = m_firstVisibleLine + visibleLineCount() - 1;
    const int lastLine = qBound(0, lastVisible + kHighlightCacheOverscanLines, maxLine);
    if (lastLine < firstLine) {
        m_cachedVisibleMatches.clear();
        m_visibleMatchCacheSize = 0;
        m_highlightCacheDirty = false;
        queuePerfStatsPublish();
        return;
    }

    const int visibleStart = ctrl->lineStartOffset(m_slotIndex, firstLine);
    const int lastLineStart = ctrl->lineStartOffset(m_slotIndex, lastLine);
    const int lastLineLength = lineLengthAt(lastLine);
    const int visibleEnd = qMax(visibleStart, lastLineStart + lastLineLength);

    if (visibleEnd <= visibleStart) {
        m_cachedVisibleMatches.clear();
    } else {
        int fetchLimit = kVisibleMatchFetchLimit;
        if (m_searchQuery.length() <= 1) {
            fetchLimit = qMin(fetchLimit, kVisibleMatchFetchLimitScrolling);
        }
        m_cachedVisibleMatches = ctrl->searchMatchPositionsInRange(m_slotIndex,
                                                                   visibleStart,
                                                                   visibleEnd,
                                                                   fetchLimit);
    }

    m_highlightCacheFirstLine = firstLine;
    m_highlightCacheLastLine = lastLine;
    m_highlightCacheTextRevision = m_textRevision;
    m_highlightCacheQuery = m_searchQuery;
    m_highlightCacheDirty = false;
    m_visibleMatchCacheSize = m_cachedVisibleMatches.size();
    queuePerfStatsPublish();
}

void PaintedEditorItem::invalidateDiagnosticCache(bool clearDiagnostics)
{
    ++m_diagnosticRequestId;
    m_diagnosticCacheDirty = true;
    m_diagnosticCacheTextRevision = -1;
    m_diagnosticCacheFirstLine = -1;
    m_diagnosticCacheVisibleLineCount = -1;

    if (clearDiagnostics) {
        m_visibleDiagnostics.clear();
    }

    if (!m_occupied || m_totalLines <= 0) {
        m_diagnosticRefreshTimer.stop();
        return;
    }

    requestDiagnosticRefreshTimerStart();
}

void PaintedEditorItem::refreshVisibleDiagnostics()
{
    if (!m_diagnosticCacheDirty) {
        return;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || m_totalLines <= 0) {
        m_visibleDiagnostics.clear();
        m_diagnosticCacheDirty = false;
        return;
    }

    if (m_diagnosticWatcher) {
        return;
    }

    if (viewportMoving()) {
        if (!m_diagnosticRefreshTimer.isActive()) {
            m_diagnosticRefreshTimer.start();
        }
        return;
    }

    const int visibleCount = visibleLineCount();
    if (visibleCount <= 0) {
        m_visibleDiagnostics.clear();
        m_diagnosticCacheDirty = false;
        return;
    }

    const int visibleFirst = qBound(0, m_firstVisibleLine, qMax(0, m_totalLines - 1));
    const int visibleEndExclusive = qMin(m_totalLines, visibleFirst + visibleCount);
    const int contextFirst = qMax(0, visibleFirst - kDiagnosticContextLines);
    const int parseLineCount = qMax(0, visibleEndExclusive - contextFirst);

    QStringList lines;
    lines.reserve(parseLineCount);
    for (int i = 0; i < parseLineCount; ++i) {
        lines.push_back(ctrl->lineText(m_slotIndex, contextFirst + i));
    }

    const quint64 requestId = m_diagnosticRequestId;
    const int slotIndex = m_slotIndex;
    const int textRevision = m_textRevision;

    auto *watcher = new QFutureWatcher<DiagnosticComputeResult>(this);
    m_diagnosticWatcher = watcher;

    connect(watcher, &QFutureWatcher<DiagnosticComputeResult>::finished, this, [this, watcher]() {
        DiagnosticComputeResult result;
        bool hasResult = false;
        try {
            result = watcher->result();
            hasResult = true;
        } catch (...) {
            hasResult = false;
        }

        watcher->deleteLater();
        if (m_diagnosticWatcher == watcher) {
            m_diagnosticWatcher = nullptr;
        }

        if (hasResult
            && result.requestId == m_diagnosticRequestId
            && result.slotIndex == m_slotIndex
            && result.textRevision == m_textRevision
            && result.visibleFirstLine == m_firstVisibleLine
            && result.visibleLineCount == visibleLineCount()
            && m_occupied
            && m_totalLines > 0) {
            m_visibleDiagnostics = result.diagnostics;
            m_diagnosticCacheTextRevision = result.textRevision;
            m_diagnosticCacheFirstLine = result.visibleFirstLine;
            m_diagnosticCacheVisibleLineCount = result.visibleLineCount;
            m_diagnosticCacheDirty = false;
            update();
            return;
        }

        if (m_diagnosticCacheDirty && m_occupied && m_totalLines > 0) {
            requestDiagnosticRefreshTimerStart();
        }
    });

    watcher->setFuture(QtConcurrent::run(
        [requestId, slotIndex, textRevision, visibleFirst, visibleCount, contextFirst, lines]() {
            DiagnosticComputeResult computeResult;
            computeResult.requestId = requestId;
            computeResult.slotIndex = slotIndex;
            computeResult.textRevision = textRevision;
            computeResult.visibleFirstLine = visibleFirst;
            computeResult.visibleLineCount = visibleCount;

            nc::Parser parser;
            const nc::ParseResult parseResult = parser.parseLines(lines.size(), [&lines](int relativeLine) {
                if (relativeLine < 0 || relativeLine >= lines.size()) {
                    return QString();
                }
                return lines.at(relativeLine);
            });

            const int visibleEndExclusive = visibleFirst + visibleCount;
            computeResult.diagnostics.reserve(parseResult.diagnostics.size());
            for (const nc::Diagnostic &diagnostic : parseResult.diagnostics) {
                const int absoluteLine = contextFirst + diagnostic.line - 1;
                if (absoluteLine < visibleFirst || absoluteLine >= visibleEndExclusive) {
                    continue;
                }

                DiagnosticMarker marker;
                marker.line = absoluteLine;
                marker.column = qMax(1, diagnostic.column);
                marker.length = qMax(1, diagnostic.length);
                marker.severity = static_cast<int>(diagnostic.severity);
                marker.code = diagnostic.code;
                const QString localizedMessage = nc::diagnosticMessage(diagnostic.code);
                marker.message = localizedMessage.isEmpty() ? diagnostic.message : localizedMessage;
                computeResult.diagnostics.push_back(marker);
            }

            return computeResult;
        }));
}

void PaintedEditorItem::requestDiagnosticRefreshTimerStart()
{
    if (m_diagnosticRefreshStartQueued.exchange(true)) {
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        m_diagnosticRefreshStartQueued.store(false);
        if (!m_diagnosticRefreshTimer.isActive()) {
            m_diagnosticRefreshTimer.start();
        }
    }, Qt::QueuedConnection);
}

QVector<int> PaintedEditorItem::diagnosticIndexesForLine(int line) const
{
    QVector<int> indexes;
    for (int i = 0; i < m_visibleDiagnostics.size(); ++i) {
        if (m_visibleDiagnostics.at(i).line == line) {
            indexes.push_back(i);
        }
    }
    return indexes;
}

int PaintedEditorItem::primaryDiagnosticIndexForLine(int line) const
{
    int best = -1;
    for (int i = 0; i < m_visibleDiagnostics.size(); ++i) {
        const DiagnosticMarker &marker = m_visibleDiagnostics.at(i);
        if (marker.line != line) {
            continue;
        }
        if (best < 0
            || diagnosticRank(marker.severity) > diagnosticRank(m_visibleDiagnostics.at(best).severity)) {
            best = i;
        }
    }
    return best;
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
    return (QDateTime::currentMSecsSinceEpoch() - m_lastViewportMotionMs) <= kViewportMotionWindowMs;
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
    if (m_totalLines <= 0) {
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
    invalidateDiagnosticCache(false);
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
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || m_totalLines <= 0) {
        m_cursorLine = 0;
        m_cursorColumn = 0;
        m_cursorXInLine = 0.0;
        return;
    }

    m_cursorOffset = clampedOffset(m_cursorOffset);
    m_cursorLine = qBound(0, ctrl->lineForOffset(m_slotIndex, m_cursorOffset), qMax(0, m_totalLines - 1));
    const int start = ctrl->lineStartOffset(m_slotIndex, m_cursorLine);
    const int lineLength = lineLengthAt(m_cursorLine);
    m_cursorColumn = qBound(0, m_cursorOffset - start, lineLength);
    if (lineLength > kLongLineApproxThreshold) {
        QFont font(QStringLiteral("Consolas"));
        font.setPixelSize(kTextPixelSize);
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
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied) {
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
        ctrl->updateCursorPosition(m_slotIndex, m_cursorOffset);
    }

    if (hasActiveFocus()) {
        QGuiApplication::inputMethod()->update(kInputMethodUpdateQueries);
    }
    update();
}

bool PaintedEditorItem::ensureEditable()
{
    if (m_canEdit) {
        return true;
    }
    emit toastRequested(QStringLiteral("\u4fdd\u5b58\u8fdb\u884c\u4e2d\uff0c\u7981\u6b62\u4fee\u6539\u6587\u4ef6\u5185\u5bb9\u3002"));
    return false;
}

bool PaintedEditorItem::applyTextEdit(int position, int removeLength, const QString &insertedText)
{
    if (!ensureEditable()) {
        return false;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
        return false;
    }

    const int newPos = ctrl->applyTextEdit(m_slotIndex, position, removeLength, insertedText);
    if (newPos < 0) {
        return false;
    }

    clearLineCache();
    invalidateDiagnosticCache(true);
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
    if (!m_occupied) {
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
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied) {
        return QStringLiteral("\n");
    }

    const int total = ctrl->textLength(m_slotIndex);
    if (total <= 0) {
        return QStringLiteral("\n");
    }

    const int sampleLength = qMin(total, 64 * 1024);
    const QString head = ctrl->textSlice(m_slotIndex, 0, sampleLength);

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

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied || m_cursorOffset < 2) {
        return 1;
    }

    const QString prevTwo = ctrl->textSlice(m_slotIndex, m_cursorOffset - 2, 2);
    if (prevTwo == QStringLiteral("\r\n")) {
        return 2;
    }

    return 1;
}

int PaintedEditorItem::deleteRemoveLength() const
{
    WorkspaceController *ctrl = workspaceController();
    if (!ctrl || !m_occupied) {
        return 0;
    }

    const int total = ctrl->textLength(m_slotIndex);
    if (m_cursorOffset < 0 || m_cursorOffset >= total) {
        return 0;
    }

    if (m_cursorOffset + 1 < total) {
        const QString nextTwo = ctrl->textSlice(m_slotIndex, m_cursorOffset, 2);
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
    if (!m_occupied || m_totalLines <= 0) {
        return;
    }

    if (m_preferredColumn < 0) {
        m_preferredColumn = m_cursorColumn;
    }

    const int targetLine = qBound(0, m_cursorLine + step, m_totalLines - 1);
    setCursorOffset(offsetForLineColumn(targetLine, m_preferredColumn), keepSelection, true);
}

void PaintedEditorItem::moveToLineBoundary(bool toLineStart, bool keepSelection)
{
    if (!m_occupied || m_totalLines <= 0) {
        return;
    }

    WorkspaceController *ctrl = workspaceController();
    if (!ctrl) {
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
        m_autoScrollTimerId = startTimer(kAutoScrollTickMs);
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
    if (!m_occupied) {
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

    if (m_occupied && m_totalLines > 0) {
        WorkspaceController *ctrl = workspaceController();
        if (ctrl) {
            QFont font(QStringLiteral("Consolas"));
            font.setPixelSize(kTextPixelSize);
            QFontMetrics fm(font);
            const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));

            const int count = visibleLineCount();
            for (int i = 0; i < count; ++i) {
                const int lineIndex = m_firstVisibleLine + i;
                if (lineIndex < 0 || lineIndex >= m_totalLines) {
                    break;
                }
                const int lineLength = lineLengthAt(lineIndex);
                if (lineLength > kLongLineApproxThreshold) {
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

void PaintedEditorItem::queuePerfStatsPublish()
{
    if (!m_perfStatsEnabled) {
        return;
    }
    requestPerfPublishTimerStart();
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

void PaintedEditorItem::requestPerfPublishTimerStart()
{
    if (QThread::currentThread() == thread()) {
        if (!m_perfPublishTimer.isActive()) {
            m_perfPublishTimer.start();
        }
        return;
    }

    bool expected = false;
    if (!m_perfPublishStartQueued.compare_exchange_strong(expected, true)) {
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        m_perfPublishStartQueued.store(false);
        if (!m_perfPublishTimer.isActive()) {
            m_perfPublishTimer.start();
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
    if (lineText.length() > kLongLineApproxThreshold) {
        const qreal monoCharWidth = qMax<qreal>(1.0, fm.horizontalAdvance(QLatin1Char('M')));
        width = monoCharWidth * static_cast<qreal>(lineText.length());
    } else {
        width = static_cast<qreal>(fm.horizontalAdvance(lineText));
    }
    m_lineWidthCache.insert(line, width);
    return width;
}
