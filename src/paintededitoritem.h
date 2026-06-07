#ifndef PAINTEDEDITORITEM_H
#define PAINTEDEDITORITEM_H

#include <atomic>
#include <QHash>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

class DocumentSession;

class PaintedEditorItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *documentSession READ documentSession WRITE setDocumentSession NOTIFY documentSessionChanged)
    Q_PROPERTY(bool editBlocked READ editBlocked WRITE setEditBlocked NOTIFY editBlockedChanged)
    Q_PROPERTY(qreal scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollMetricsChanged)
    Q_PROPERTY(qreal scrollSize READ scrollSize NOTIFY scrollMetricsChanged)
    Q_PROPERTY(qreal horizontalScrollPosition READ horizontalScrollPosition WRITE setHorizontalScrollPosition NOTIFY scrollMetricsChanged)
    Q_PROPERTY(qreal horizontalScrollSize READ horizontalScrollSize NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int pasteLimitBytes READ pasteLimitBytes WRITE setPasteLimitBytes NOTIFY pasteLimitBytesChanged)

public:
    explicit PaintedEditorItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QObject *documentSession() const;
    void setDocumentSession(QObject *documentSession);

    bool editBlocked() const;
    void setEditBlocked(bool blocked);

    qreal scrollPosition() const;
    void setScrollPosition(qreal position);

    qreal scrollSize() const;
    qreal horizontalScrollPosition() const;
    void setHorizontalScrollPosition(qreal position);
    qreal horizontalScrollSize() const;
    int pasteLimitBytes() const;
    void setPasteLimitBytes(int bytes);

    Q_INVOKABLE void selectByOffset(int offset);
    Q_INVOKABLE void selectRange(int start, int length);
    Q_INVOKABLE void performCopy();
    Q_INVOKABLE void performCut();
    Q_INVOKABLE void performPaste();
    Q_INVOKABLE void performDelete();
    Q_INVOKABLE void performUndo();
    Q_INVOKABLE void performRedo();

signals:
    void documentSessionChanged();
    void editBlockedChanged();
    void scrollMetricsChanged();
    void pasteLimitBytesChanged();
    void toastRequested(const QString &message);
    void focusRequested();
    void menuRequested(qreal x, qreal y);
    void findRequested();

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
    DocumentSession *document() const;
    bool multiSelectEnabled() const;
    bool occupied() const;
    bool canEdit() const;
    int totalLines() const;
    int currentLine() const;
    int textRevision() const;
    QString searchQuery() const;
    void resetDocumentViewState();
    void handleDocumentLineCountChanged();
    void handleDocumentTextRevisionChanged();
    void handleDocumentSearchStateChanged();
    void handleDocumentEditCapabilitiesChanged();
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
    void requestHighlightRefreshTimerStart();

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

    QPointer<DocumentSession> m_documentSession;
    bool m_editBlocked = false;
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
    QTimer m_horizontalMetricsTimer;
    int m_autoScrollTimerId = 0;
    int m_autoScrollDirection = 0;
    qint64 m_lastViewportMotionMs = 0;
    int m_pasteLimitBytes = 0;

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
    std::atomic_bool m_highlightRefreshStartQueued{false};
};

#endif // PAINTEDEDITORITEM_H
