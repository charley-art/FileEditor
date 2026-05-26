#ifndef PAINTEDEDITORITEM_H
#define PAINTEDEDITORITEM_H

#include <atomic>
#include <QHash>
#include <QPointer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QQuickPaintedItem>
#include <QTimer>

class WorkspaceController;

class PaintedEditorItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(int slotIndex READ slotIndex WRITE setSlotIndex NOTIFY slotIndexChanged)
    Q_PROPERTY(bool occupied READ occupied WRITE setOccupied NOTIFY occupiedChanged)
    Q_PROPERTY(bool canEdit READ canEdit WRITE setCanEdit NOTIFY canEditChanged)
    Q_PROPERTY(int totalLines READ totalLines WRITE setTotalLines NOTIFY totalLinesChanged)
    Q_PROPERTY(int currentLine READ currentLine WRITE setCurrentLine NOTIFY currentLineChanged)
    Q_PROPERTY(int textRevision READ textRevision WRITE setTextRevision NOTIFY textRevisionChanged)
    Q_PROPERTY(bool multiSelectEnabled READ multiSelectEnabled WRITE setMultiSelectEnabled NOTIFY multiSelectEnabledChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(qreal scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollMetricsChanged)
    Q_PROPERTY(qreal scrollSize READ scrollSize NOTIFY scrollMetricsChanged)
    Q_PROPERTY(qreal horizontalScrollPosition READ horizontalScrollPosition WRITE setHorizontalScrollPosition NOTIFY scrollMetricsChanged)
    Q_PROPERTY(qreal horizontalScrollSize READ horizontalScrollSize NOTIFY scrollMetricsChanged)
    Q_PROPERTY(bool perfStatsEnabled READ perfStatsEnabled WRITE setPerfStatsEnabled NOTIFY perfStatsEnabledChanged)
    Q_PROPERTY(qreal lastPaintMs READ lastPaintMs NOTIFY perfStatsChanged)
    Q_PROPERTY(qreal averagePaintMs READ averagePaintMs NOTIFY perfStatsChanged)
    Q_PROPERTY(int paintFps READ paintFps NOTIFY perfStatsChanged)
    Q_PROPERTY(int visibleMatchCacheSize READ visibleMatchCacheSize NOTIFY perfStatsChanged)

public:
    explicit PaintedEditorItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QObject *controller() const;
    void setController(QObject *controller);

    int slotIndex() const;
    void setSlotIndex(int slotIndex);

    bool occupied() const;
    void setOccupied(bool occupied);

    bool canEdit() const;
    void setCanEdit(bool canEdit);

    int totalLines() const;
    void setTotalLines(int totalLines);

    int currentLine() const;
    void setCurrentLine(int currentLine);

    int textRevision() const;
    void setTextRevision(int textRevision);

    bool multiSelectEnabled() const;
    void setMultiSelectEnabled(bool enabled);
    QString searchQuery() const;
    void setSearchQuery(const QString &query);

    qreal scrollPosition() const;
    void setScrollPosition(qreal position);

    qreal scrollSize() const;
    qreal horizontalScrollPosition() const;
    void setHorizontalScrollPosition(qreal position);
    qreal horizontalScrollSize() const;
    bool perfStatsEnabled() const;
    void setPerfStatsEnabled(bool enabled);
    qreal lastPaintMs() const;
    qreal averagePaintMs() const;
    int paintFps() const;
    int visibleMatchCacheSize() const;

    Q_INVOKABLE void selectByOffset(int offset);
    Q_INVOKABLE void selectRange(int start, int length);
    Q_INVOKABLE void performCopy();
    Q_INVOKABLE void performCut();
    Q_INVOKABLE void performPaste();
    Q_INVOKABLE void performDelete();
    Q_INVOKABLE void performUndo();
    Q_INVOKABLE void performRedo();

signals:
    void controllerChanged();
    void slotIndexChanged();
    void occupiedChanged();
    void canEditChanged();
    void totalLinesChanged();
    void currentLineChanged();
    void textRevisionChanged();
    void multiSelectEnabledChanged();
    void searchQueryChanged();
    void scrollMetricsChanged();
    void perfStatsEnabledChanged();
    void toastRequested(const QString &message);
    void focusRequested(int slot);
    void menuRequested(qreal x, qreal y);
    void findRequested(int slot);
    void perfStatsChanged();

protected:
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void timerEvent(QTimerEvent *event) override;

private:
    WorkspaceController *workspaceController() const;
    int visibleLineCount() const;
    int maxFirstVisibleLine() const;
    int clampedOffset(int offset) const;
    int lineNumberWidth() const;
    qreal textAreaLeft() const;
    int lineAtY(qreal y) const;
    int offsetForPoint(const QPointF &point) const;
    int offsetForLineColumn(int line, int column) const;
    int lineLengthAt(int line) const;
    int columnForX(const QString &lineText, qreal xInText) const;
    qreal xForColumn(const QString &lineText, int column) const;
    QString lineTextAt(int line) const;
    qreal lineWidthAt(int line, const QString &lineText, const QFontMetrics &fm) const;
    void clearLineCache();
    void pruneLineCache();
    void invalidateHighlightCache(bool clearMatches);
    void refreshVisibleMatchCache();
    void markViewportMoved();
    bool viewportMoving() const;
    qreal textViewportWidth() const;
    qreal maxHorizontalOffset() const;
    void ensureContentWidthForCursor();
    void setHorizontalOffsetInternal(qreal offset);
    void updateHorizontalMetrics();
    void queuePerfStatsPublish();
    void requestHighlightRefreshTimerStart();
    void invalidateDiagnosticCache(bool clearDiagnostics);
    void refreshVisibleDiagnostics();
    void requestDiagnosticRefreshTimerStart();
    QVector<int> diagnosticIndexesForLine(int line) const;
    int primaryDiagnosticIndexForLine(int line) const;
    void requestPerfPublishTimerStart();

    bool hasSelection() const;
    int selectionStart() const;
    int selectionEnd() const;
    void clearSelectionToCursor();
    void ensureCursorVisible();
    void setFirstVisibleLineInternal(int line);
    void syncCursorFromOffset();
    void setCursorOffset(int offset, bool keepSelection, bool notifyController);
    bool ensureEditable();
    bool applyTextEdit(int position, int removeLength, const QString &insertedText);
    bool deleteCurrentSelection();
    void insertText(const QString &text);
    QString preferredLineBreak() const;
    int backspaceRemoveLength() const;
    int deleteRemoveLength() const;
    void moveHorizontal(int step, bool keepSelection);
    void moveVertical(int step, bool keepSelection);
    void moveToLineBoundary(bool toLineStart, bool keepSelection);

    void startLongPress(const QPointF &pos);
    void stopLongPress();
    void startAutoScroll(int direction);
    void stopAutoScroll();

    QObject *m_controller = nullptr;
    int m_slotIndex = -1;
    bool m_occupied = false;
    bool m_canEdit = false;
    int m_totalLines = 0;
    int m_currentLine = 0;
    int m_textRevision = 0;
    bool m_multiSelectEnabled = false;
    QString m_searchQuery;
    QString m_preeditText;
    int m_preeditCursorInString = -1;

    int m_firstVisibleLine = 0;
    int m_cursorOffset = 0;
    int m_cursorLine = 0;
    int m_cursorColumn = 0;
    qreal m_cursorXInLine = 0.0;
    qreal m_horizontalOffset = 0.0;
    qreal m_contentWidth = 0.0;
    int m_preferredColumn = -1;

    int m_selectionAnchorOffset = -1;
    int m_selectionCursorOffset = -1;

    bool m_mousePressed = false;
    bool m_dragStarted = false;
    bool m_pressInsideSelection = false;
    int m_pressOffset = 0;
    QPointF m_pressPoint;
    QPointF m_lastMousePoint;
    QTimer m_longPressTimer;
    QTimer m_highlightRefreshTimer;
    QTimer m_diagnosticRefreshTimer;
    QTimer m_horizontalMetricsTimer;
    QTimer m_perfPublishTimer;
    int m_autoScrollTimerId = 0;
    int m_autoScrollDirection = 0;
    qint64 m_lastViewportMotionMs = 0;
    QElapsedTimer m_perfClock;
    qint64 m_perfWindowStartMs = 0;
    int m_perfFrameCount = 0;
    qreal m_lastPaintMs = 0.0;
    qreal m_averagePaintMs = 0.0;
    int m_paintFps = 0;
    int m_visibleMatchCacheSize = 0;
    bool m_perfStatsEnabled = false;

    mutable QHash<int, QString> m_lineCache;
    mutable QHash<int, int> m_lineLengthCache;
    mutable QHash<int, qreal> m_lineWidthCache;
    mutable int m_cachedLineNumberDigits = -1;
    mutable int m_cachedLineNumberWidth = 64;
    QVector<int> m_cachedVisibleMatches;
    bool m_highlightCacheDirty = true;
    int m_highlightCacheFirstLine = -1;
    int m_highlightCacheLastLine = -1;
    int m_highlightCacheTextRevision = -1;
    QString m_highlightCacheQuery;
    struct DiagnosticMarker {
        int line = 0;
        int column = 1;
        int length = 1;
        int severity = 0;
        QString code;
        QString message;
    };
    struct DiagnosticComputeResult {
        quint64 requestId = 0;
        int slotIndex = -1;
        int textRevision = -1;
        int visibleFirstLine = -1;
        int visibleLineCount = 0;
        QVector<DiagnosticMarker> diagnostics;
    };
    QVector<DiagnosticMarker> m_visibleDiagnostics;
    bool m_diagnosticCacheDirty = true;
    int m_diagnosticCacheTextRevision = -1;
    int m_diagnosticCacheFirstLine = -1;
    int m_diagnosticCacheVisibleLineCount = -1;
    quint64 m_diagnosticRequestId = 0;
    QPointer<QFutureWatcher<DiagnosticComputeResult>> m_diagnosticWatcher;
    std::atomic_bool m_highlightRefreshStartQueued{false};
    std::atomic_bool m_diagnosticRefreshStartQueued{false};
    std::atomic_bool m_perfPublishStartQueued{false};
};

#endif // PAINTEDEDITORITEM_H
