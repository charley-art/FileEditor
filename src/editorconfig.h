#ifndef EDITORCONFIG_H
#define EDITORCONFIG_H

#include <QObject>

class EditorConfig : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int maxPaneCount READ maxPaneCount CONSTANT)
    Q_PROPERTY(int defaultPasteLimitBytes READ defaultPasteLimitBytes CONSTANT)
    Q_PROPERTY(int minPasteLimitBytes READ minPasteLimitBytes CONSTANT)
    Q_PROPERTY(int maxPasteLimitBytes READ maxPasteLimitBytes CONSTANT)
    Q_PROPERTY(int undoHistoryMaxBytes READ undoHistoryMaxBytes CONSTANT)
    Q_PROPERTY(int undoEntryMaxBytes READ undoEntryMaxBytes CONSTANT)
    Q_PROPERTY(int undoHistoryMaxCommands READ undoHistoryMaxCommands CONSTANT)
    Q_PROPERTY(int asyncSearchThresholdChars READ asyncSearchThresholdChars CONSTANT)
    Q_PROPERTY(int searchDebounceMs READ searchDebounceMs CONSTANT)
    Q_PROPERTY(int searchDebounceLargeMs READ searchDebounceLargeMs CONSTANT)
    Q_PROPERTY(int largeSearchDebounceThresholdChars READ largeSearchDebounceThresholdChars CONSTANT)
    Q_PROPERTY(int fullTextCacheThresholdChars READ fullTextCacheThresholdChars CONSTANT)
    Q_PROPERTY(qint64 mappedDecodeThresholdBytes READ mappedDecodeThresholdBytes CONSTANT)
    Q_PROPERTY(qint64 maxOpenFileBytes READ maxOpenFileBytes CONSTANT)
    Q_PROPERTY(int maxDocumentChars READ maxDocumentChars CONSTANT)
    Q_PROPERTY(int searchHighlightLimit READ searchHighlightLimit CONSTANT)
    Q_PROPERTY(int replaceAllLimit READ replaceAllLimit CONSTANT)
    Q_PROPERTY(int lineHeight READ lineHeight CONSTANT)
    Q_PROPERTY(int textPixelSize READ textPixelSize CONSTANT)
    Q_PROPERTY(int longPressMs READ longPressMs CONSTANT)
    Q_PROPERTY(int autoScrollTickMs READ autoScrollTickMs CONSTANT)
    Q_PROPERTY(int selectionDragThreshold READ selectionDragThreshold CONSTANT)
    Q_PROPERTY(int maxFindHighlightsPerLine READ maxFindHighlightsPerLine CONSTANT)
    Q_PROPERTY(int visibleMatchFetchLimit READ visibleMatchFetchLimit CONSTANT)
    Q_PROPERTY(int visibleMatchFetchLimitScrolling READ visibleMatchFetchLimitScrolling CONSTANT)
    Q_PROPERTY(int longLineApproxThreshold READ longLineApproxThreshold CONSTANT)
    Q_PROPERTY(int paintOverscanColumns READ paintOverscanColumns CONSTANT)
    Q_PROPERTY(int longLineHighlightOverscanColumns READ longLineHighlightOverscanColumns CONSTANT)
    Q_PROPERTY(int lineCacheMarginLines READ lineCacheMarginLines CONSTANT)
    Q_PROPERTY(int highlightRefreshMs READ highlightRefreshMs CONSTANT)
    Q_PROPERTY(int highlightCacheOverscanLines READ highlightCacheOverscanLines CONSTANT)
    Q_PROPERTY(int highlightFrameBudgetIdle READ highlightFrameBudgetIdle CONSTANT)
    Q_PROPERTY(int highlightFrameBudgetScrolling READ highlightFrameBudgetScrolling CONSTANT)
    Q_PROPERTY(int viewportMotionWindowMs READ viewportMotionWindowMs CONSTANT)
    Q_PROPERTY(int horizontalMetricsRefreshMs READ horizontalMetricsRefreshMs CONSTANT)

public:
    static EditorConfig &instance();

    int maxPaneCount() const;
    int defaultPasteLimitBytes() const;
    int minPasteLimitBytes() const;
    int maxPasteLimitBytes() const;

    int undoHistoryMaxBytes() const;
    int undoEntryMaxBytes() const;
    int undoHistoryMaxCommands() const;
    int asyncSearchThresholdChars() const;
    int searchDebounceMs() const;
    int searchDebounceLargeMs() const;
    int largeSearchDebounceThresholdChars() const;
    int fullTextCacheThresholdChars() const;
    qint64 mappedDecodeThresholdBytes() const;
    qint64 maxOpenFileBytes() const;
    int maxDocumentChars() const;
    int searchHighlightLimit() const;
    int replaceAllLimit() const;

    int lineHeight() const;
    int textPixelSize() const;
    int longPressMs() const;
    int autoScrollTickMs() const;
    int selectionDragThreshold() const;
    int maxFindHighlightsPerLine() const;
    int visibleMatchFetchLimit() const;
    int visibleMatchFetchLimitScrolling() const;
    int longLineApproxThreshold() const;
    int paintOverscanColumns() const;
    int longLineHighlightOverscanColumns() const;
    int lineCacheMarginLines() const;
    int highlightRefreshMs() const;
    int highlightCacheOverscanLines() const;
    int highlightFrameBudgetIdle() const;
    int highlightFrameBudgetScrolling() const;
    int viewportMotionWindowMs() const;
    int horizontalMetricsRefreshMs() const;

private:
    explicit EditorConfig(QObject *parent = nullptr);
};

#endif // EDITORCONFIG_H
