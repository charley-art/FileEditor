#include "editorconfig.h"

EditorConfig::EditorConfig(QObject *parent)
    : QObject(parent)
{
}

EditorConfig &EditorConfig::instance()
{
    static EditorConfig config;
    return config;
}

int EditorConfig::maxPaneCount() const { return 4; }
int EditorConfig::defaultPasteLimitBytes() const { return 10 * 1024; }
int EditorConfig::minPasteLimitBytes() const { return 1024; }
int EditorConfig::maxPasteLimitBytes() const { return 1024 * 1024; }

int EditorConfig::undoHistoryMaxBytes() const { return 8 * 1024 * 1024; }
int EditorConfig::undoEntryMaxBytes() const { return 512 * 1024; }
int EditorConfig::undoHistoryMaxCommands() const { return 256; }
int EditorConfig::asyncSearchThresholdChars() const { return 256 * 1024; }
int EditorConfig::searchDebounceMs() const { return 80; }
int EditorConfig::searchDebounceLargeMs() const { return 180; }
int EditorConfig::largeSearchDebounceThresholdChars() const { return 5 * 1024 * 1024; }
int EditorConfig::fullTextCacheThresholdChars() const { return 8 * 1024 * 1024; }
qint64 EditorConfig::mappedDecodeThresholdBytes() const { return 8ll * 1024ll * 1024ll; }
qint64 EditorConfig::maxOpenFileBytes() const { return 200ll * 1024ll * 1024ll; }
int EditorConfig::maxDocumentChars() const { return 150 * 1024 * 1024; }
int EditorConfig::searchHighlightLimit() const { return 1000; }
int EditorConfig::replaceAllLimit() const { return 200; }

int EditorConfig::lineHeight() const { return 34; }
int EditorConfig::textPixelSize() const { return 20; }
int EditorConfig::longPressMs() const { return 450; }
int EditorConfig::autoScrollTickMs() const { return 30; }
int EditorConfig::selectionDragThreshold() const { return 4; }
int EditorConfig::maxFindHighlightsPerLine() const { return 512; }
int EditorConfig::visibleMatchFetchLimit() const { return 1000; }
int EditorConfig::visibleMatchFetchLimitScrolling() const { return 320; }
int EditorConfig::longLineApproxThreshold() const { return 4096; }
int EditorConfig::paintOverscanColumns() const { return 24; }
int EditorConfig::longLineHighlightOverscanColumns() const { return 48; }
int EditorConfig::lineCacheMarginLines() const { return 256; }
int EditorConfig::highlightRefreshMs() const { return 70; }
int EditorConfig::highlightCacheOverscanLines() const { return 12; }
int EditorConfig::highlightFrameBudgetIdle() const { return 720; }
int EditorConfig::highlightFrameBudgetScrolling() const { return 200; }
int EditorConfig::viewportMotionWindowMs() const { return 140; }
int EditorConfig::horizontalMetricsRefreshMs() const { return 80; }
