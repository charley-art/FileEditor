#include "documentsession.h"

#include <QFile>
#include <QSaveFile>
#include <QTextCodec>
#include <QtConcurrent>

#include <algorithm>
#include <exception>
#include <limits>
#include <new>

namespace {
constexpr int kUndoHistoryMaxBytes = 8 * 1024 * 1024;
constexpr int kUndoEntryMaxBytes = 512 * 1024;
constexpr int kUndoHistoryMaxCommands = 256;
constexpr int kAsyncSearchThresholdCharsDefault = 256 * 1024;
constexpr int kSearchDebounceMsDefault = 80;
constexpr int kSearchDebounceLargeMsDefault = 180;
constexpr int kLargeSearchDebounceThresholdCharsDefault = 5 * 1024 * 1024;
constexpr int kFullTextCacheThresholdChars = 8 * 1024 * 1024;
constexpr qint64 kMappedDecodeThresholdBytesDefault = 8ll * 1024ll * 1024ll;
constexpr qint64 kMaxOpenFileBytesDefault = 200ll * 1024ll * 1024ll;
constexpr int kMaxDocumentCharsDefault = 150 * 1024 * 1024;

int readEnvInt(const char *name, int fallback, int minValue, int maxValue)
{
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const int parsed = QString::fromLatin1(value).toInt(&ok);
    if (!ok) {
        return fallback;
    }

    return qBound(minValue, parsed, maxValue);
}

qint64 readEnvInt64(const char *name, qint64 fallback, qint64 minValue, qint64 maxValue)
{
    const QByteArray value = qgetenv(name);
    if (value.isEmpty()) {
        return fallback;
    }

    bool ok = false;
    const qint64 parsed = QString::fromLatin1(value).toLongLong(&ok);
    if (!ok) {
        return fallback;
    }

    return qBound(minValue, parsed, maxValue);
}

struct RuntimeTuning {
    int asyncSearchThresholdChars = kAsyncSearchThresholdCharsDefault;
    int searchDebounceMs = kSearchDebounceMsDefault;
    int searchDebounceLargeMs = kSearchDebounceLargeMsDefault;
    int largeSearchDebounceThresholdChars = kLargeSearchDebounceThresholdCharsDefault;
    qint64 mappedDecodeThresholdBytes = kMappedDecodeThresholdBytesDefault;
    qint64 maxOpenFileBytes = kMaxOpenFileBytesDefault;
    int maxDocumentChars = kMaxDocumentCharsDefault;
    int searchHighlightLimit = DocumentSession::kSearchHighlightLimit;
    int replaceAllLimit = DocumentSession::kReplaceAllLimitDefault;
};

const RuntimeTuning &runtimeTuning()
{
    static const RuntimeTuning tuning = []() {
        RuntimeTuning t;

        t.asyncSearchThresholdChars =
            readEnvInt("NCEDITOR_ASYNC_SEARCH_THRESHOLD_KB",
                       kAsyncSearchThresholdCharsDefault / 1024,
                       32,
                       8192)
            * 1024;
        t.searchDebounceMs =
            readEnvInt("NCEDITOR_SEARCH_DEBOUNCE_MS",
                       kSearchDebounceMsDefault,
                       20,
                       3000);
        t.searchDebounceLargeMs =
            readEnvInt("NCEDITOR_SEARCH_DEBOUNCE_LARGE_MS",
                       kSearchDebounceLargeMsDefault,
                       20,
                       5000);
        t.largeSearchDebounceThresholdChars =
            readEnvInt("NCEDITOR_SEARCH_DEBOUNCE_LARGE_THRESHOLD_MB",
                       kLargeSearchDebounceThresholdCharsDefault / (1024 * 1024),
                       1,
                       1024)
            * 1024
            * 1024;
        t.mappedDecodeThresholdBytes =
            readEnvInt64("NCEDITOR_MAPPED_DECODE_THRESHOLD_MB",
                         kMappedDecodeThresholdBytesDefault / (1024ll * 1024ll),
                         1,
                         1024)
            * 1024ll
            * 1024ll;
        t.maxOpenFileBytes =
            readEnvInt64("NCEDITOR_MAX_OPEN_FILE_MB",
                         kMaxOpenFileBytesDefault / (1024ll * 1024ll),
                         16,
                         2048)
            * 1024ll
            * 1024ll;
        t.maxDocumentChars =
            readEnvInt("NCEDITOR_MAX_DOCUMENT_MB",
                       kMaxDocumentCharsDefault / (1024 * 1024),
                       16,
                       1024)
            * 1024
            * 1024;
        t.searchHighlightLimit =
            readEnvInt("NCEDITOR_SEARCH_HIGHLIGHT_LIMIT",
                       DocumentSession::kSearchHighlightLimit,
                       100,
                       200000);
        t.replaceAllLimit =
            readEnvInt("NCEDITOR_REPLACE_ALL_LIMIT",
                       DocumentSession::kReplaceAllLimitDefault,
                       1,
                       50000);
        return t;
    }();
    return tuning;
}

enum class Utf16Heuristic {
    None,
    LittleEndian,
    BigEndian
};

Utf16Heuristic detectUtf16WithoutBom(const QByteArray &bytes)
{
    const int sampleLen = qMin(bytes.size(), 8192);
    if (sampleLen < 8) {
        return Utf16Heuristic::None;
    }

    int evenZero = 0;
    int oddZero = 0;
    int evenAscii = 0;
    int oddAscii = 0;

    for (int i = 0; i < sampleLen; ++i) {
        const unsigned char b = static_cast<unsigned char>(bytes.at(i));
        const bool isAsciiLike = (b == '\n' || b == '\r' || b == '\t' || (b >= 0x20 && b <= 0x7E));
        if ((i & 1) == 0) {
            if (b == 0) {
                ++evenZero;
            } else if (isAsciiLike) {
                ++evenAscii;
            }
        } else {
            if (b == 0) {
                ++oddZero;
            } else if (isAsciiLike) {
                ++oddAscii;
            }
        }
    }

    const int evenCount = (sampleLen + 1) / 2;
    const int oddCount = sampleLen / 2;
    if (evenCount <= 0 || oddCount <= 0) {
        return Utf16Heuristic::None;
    }

    const double evenZeroRatio = static_cast<double>(evenZero) / static_cast<double>(evenCount);
    const double oddZeroRatio = static_cast<double>(oddZero) / static_cast<double>(oddCount);

    if (oddZeroRatio > 0.35 && evenZeroRatio < 0.1 && evenAscii > (oddAscii * 2)) {
        return Utf16Heuristic::LittleEndian;
    }

    if (evenZeroRatio > 0.35 && oddZeroRatio < 0.1 && oddAscii > (evenAscii * 2)) {
        return Utf16Heuristic::BigEndian;
    }

    return Utf16Heuristic::None;
}

bool encodeAndWriteRange(QSaveFile *file,
                         QTextEncoder *encoder,
                         const QString &source,
                         int start,
                         int length,
                         QString *error)
{
    static const int kChunkSizes[] = {
        64 * 1024,
        16 * 1024,
        4 * 1024,
        1024,
        256
    };

    int offset = 0;
    while (offset < length) {
        const int remaining = length - offset;
        bool encoded = false;

        for (const int chunkSize : kChunkSizes) {
            const int len = qMin(chunkSize, remaining);
            if (len <= 0) {
                continue;
            }

            try {
                const QByteArray encodedChunk =
                    encoder->fromUnicode(source.constData() + start + offset, len);
                if (!encodedChunk.isEmpty() && file->write(encodedChunk) != encodedChunk.size()) {
                    if (error) {
                        *error = QStringLiteral("写入文件失败: %1").arg(file->errorString());
                    }
                    return false;
                }
                offset += len;
                encoded = true;
                break;
            } catch (const std::bad_alloc &) {
                // Retry with smaller chunk size.
                continue;
            } catch (const std::exception &ex) {
                if (error) {
                    *error = QStringLiteral("保存失败：%1").arg(QString::fromUtf8(ex.what()));
                }
                return false;
            } catch (...) {
                if (error) {
                    *error = QStringLiteral("保存失败：未知异常。");
                }
                return false;
            }
        }

        if (!encoded) {
            if (error) {
                *error = QStringLiteral("保存失败：内存不足（编码阶段）。");
            }
            return false;
        }
    }

    return true;
}
}

DocumentSession::DocumentSession(QObject *parent)
    : QObject(parent)
    , m_codecName(QStringLiteral("UTF-8"))
    , m_latestSearchRequestId(std::make_shared<std::atomic<quint64>>(0))
{
    m_buffer.reset(QString());
    m_lineIndex.rebuild(QString());
    m_contentRevision = 1;

    m_searchDebounceTimer.setSingleShot(true);
    m_searchDebounceTimer.setInterval(runtimeTuning().searchDebounceMs);
    connect(&m_searchDebounceTimer, &QTimer::timeout, this, &DocumentSession::startQueuedSearch);
}

QString DocumentSession::filePath() const
{
    return m_filePath;
}

QString DocumentSession::displayPath() const
{
    if (m_filePath.isEmpty()) {
        return QStringLiteral("未命名");
    }
    return m_filePath;
}

QString DocumentSession::text() const
{
    if (!m_fullTextCacheEnabled) {
        return QString();
    }
    return m_cachedText;
}

QString DocumentSession::currentTextSnapshot() const
{
    if (m_fullTextCacheEnabled) {
        return m_cachedText;
    }
    return m_buffer.toString();
}

bool DocumentSession::isDirty() const
{
    return m_dirty;
}

bool DocumentSession::isSaving() const
{
    return m_saving;
}

QString DocumentSession::codecName() const
{
    return m_codecName;
}

int DocumentSession::lineCount() const
{
    return m_lineIndex.lineCount();
}

int DocumentSession::currentLine() const
{
    return m_currentLine;
}

int DocumentSession::currentLinePercent() const
{
    const int total = lineCount();
    if (total <= 0) {
        return 0;
    }
    return (m_currentLine * 100) / total;
}

int DocumentSession::cursorPosition() const
{
    return m_cursorPosition;
}

int DocumentSession::textLength() const
{
    return m_buffer.length();
}

int DocumentSession::lineStartOffset(int zeroBasedLine) const
{
    if (zeroBasedLine < 0 || zeroBasedLine >= m_lineIndex.lineCount()) {
        return 0;
    }
    return m_lineIndex.lineStart(zeroBasedLine);
}

int DocumentSession::lineLengthAt(int zeroBasedLine) const
{
    if (zeroBasedLine < 0 || zeroBasedLine >= m_lineIndex.lineCount()) {
        return 0;
    }

    const int start = m_lineIndex.lineStart(zeroBasedLine);
    int end = m_lineIndex.lineEndExclusive(zeroBasedLine);
    if (end < start) {
        end = start;
    }

    int length = end - start;
    if (length <= 0) {
        return 0;
    }

    const QChar tail = m_buffer.at(end - 1);
    if (tail == QLatin1Char('\n')) {
        if (length >= 2 && m_buffer.at(end - 2) == QLatin1Char('\r')) {
            length -= 2;
        } else {
            --length;
        }
    } else if (tail == QLatin1Char('\r')) {
        --length;
    }

    return qMax(0, length);
}

QString DocumentSession::lineTextAt(int zeroBasedLine) const
{
    if (zeroBasedLine < 0 || zeroBasedLine >= m_lineIndex.lineCount()) {
        return {};
    }

    const int start = m_lineIndex.lineStart(zeroBasedLine);
    const int length = lineLengthAt(zeroBasedLine);
    if (length <= 0) {
        return {};
    }
    return m_buffer.mid(start, length);
}

QString DocumentSession::lineTextSliceAt(int zeroBasedLine, int startColumn, int maxChars) const
{
    if (zeroBasedLine < 0 || zeroBasedLine >= m_lineIndex.lineCount() || maxChars <= 0) {
        return {};
    }

    const int lineLength = lineLengthAt(zeroBasedLine);
    if (lineLength <= 0) {
        return {};
    }

    const int safeStartColumn = qBound(0, startColumn, lineLength);
    const int safeLength = qBound(0, maxChars, lineLength - safeStartColumn);
    if (safeLength <= 0) {
        return {};
    }

    const int start = m_lineIndex.lineStart(zeroBasedLine);
    return m_buffer.mid(start + safeStartColumn, safeLength);
}

QString DocumentSession::textSlice(int start, int length) const
{
    if (length <= 0 || start >= m_buffer.length()) {
        return {};
    }

    const int safeStart = qBound(0, start, m_buffer.length());
    const int safeLength = qBound(0, length, m_buffer.length() - safeStart);
    if (safeLength <= 0) {
        return {};
    }

    return m_buffer.mid(safeStart, safeLength);
}

int DocumentSession::lineForOffsetZeroBased(int offset) const
{
    const int clamped = qBound(0, offset, m_buffer.length());
    return m_lineIndex.lineForOffset(clamped);
}

bool DocumentSession::replaceLineText(int zeroBasedLine, const QString &lineText)
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    if (zeroBasedLine < 0 || zeroBasedLine >= m_lineIndex.lineCount()) {
        return false;
    }

    QString sanitized = lineText;
    sanitized.remove(QLatin1Char('\r'));
    sanitized.remove(QLatin1Char('\n'));

    const int start = m_lineIndex.lineStart(zeroBasedLine);
    const int end = m_lineIndex.lineEndExclusive(zeroBasedLine);
    if (end < start) {
        return false;
    }

    QString oldLine = m_buffer.mid(start, end - start);
    QString terminator;
    if (oldLine.endsWith(QStringLiteral("\r\n"))) {
        terminator = QStringLiteral("\r\n");
    } else if (oldLine.endsWith(QLatin1Char('\n'))) {
        terminator = QStringLiteral("\n");
    } else if (oldLine.endsWith(QLatin1Char('\r'))) {
        terminator = QStringLiteral("\r");
    }

    QString replacement = sanitized + terminator;
    if (replacement == oldLine) {
        return true;
    }

    const bool ok = applyEditInternal(start, end - start, replacement);
    if (ok) {
        m_cursorPosition = qBound(0, start + sanitized.length(), m_buffer.length());
        updateCurrentLineFromCursor();
    }
    return ok;
}

bool DocumentSession::deleteLineAt(int zeroBasedLine)
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    const int lines = m_lineIndex.lineCount();
    if (zeroBasedLine < 0 || zeroBasedLine >= lines) {
        return false;
    }

    const int start = m_lineIndex.lineStart(zeroBasedLine);
    const int end = m_lineIndex.lineEndExclusive(zeroBasedLine);
    if (end < start) {
        return false;
    }

    int removeStart = start;
    int removeLen = end - start;

    if (lines == 1) {
        removeStart = 0;
        removeLen = m_buffer.length();
    } else if (zeroBasedLine == lines - 1) {
        if (start > 0) {
            QString prev = m_buffer.mid(start - 1, 1);
            if (prev == QStringLiteral("\n")) {
                removeStart = start - 1;
                if (removeStart > 0) {
                    QString prev2 = m_buffer.mid(removeStart - 1, 1);
                    if (prev2 == QStringLiteral("\r")) {
                        --removeStart;
                    }
                }
                removeLen = end - removeStart;
            } else if (prev == QStringLiteral("\r")) {
                removeStart = start - 1;
                removeLen = end - removeStart;
            }
        }
    }

    const bool ok = applyEditInternal(removeStart, removeLen, QString());
    if (ok) {
        m_cursorPosition = qBound(0, removeStart, m_buffer.length());
        updateCurrentLineFromCursor();
    }
    return ok;
}

bool DocumentSession::canUndo() const
{
    return !m_undoHistory.isEmpty();
}

bool DocumentSession::canRedo() const
{
    return !m_redoHistory.isEmpty();
}

bool DocumentSession::undo()
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    if (m_undoHistory.isEmpty()) {
        return false;
    }

    const EditCommand cmd = m_undoHistory.takeLast();
    m_undoBytes -= commandBytes(cmd);
    if (m_undoBytes < 0) {
        m_undoBytes = 0;
    }

    if (!applyEditInternal(cmd.position, cmd.insertedText.length(), cmd.removedText, false)) {
        return false;
    }

    m_cursorPosition = qBound(0, cmd.cursorBefore, m_buffer.length());
    updateCurrentLineFromCursor();

    if (!pushRedoCommand(cmd)) {
        m_redoHistory.clear();
        m_redoBytes = 0;
    }

    m_stateId = cmd.fromState;
    setDirtyInternal(m_stateId != m_savedStateId);
    emit editCapabilitiesChanged();
    return true;
}

bool DocumentSession::redo()
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    if (m_redoHistory.isEmpty()) {
        return false;
    }

    const EditCommand cmd = m_redoHistory.takeLast();
    m_redoBytes -= commandBytes(cmd);
    if (m_redoBytes < 0) {
        m_redoBytes = 0;
    }

    if (!applyEditInternal(cmd.position, cmd.removedText.length(), cmd.insertedText, false)) {
        return false;
    }

    m_cursorPosition = qBound(0, cmd.cursorAfter, m_buffer.length());
    updateCurrentLineFromCursor();

    if (!pushUndoCommand(cmd)) {
        m_undoHistory.clear();
        m_undoBytes = 0;
    }

    m_stateId = cmd.toState;
    setDirtyInternal(m_stateId != m_savedStateId);
    emit editCapabilitiesChanged();
    return true;
}

QString DocumentSession::searchQuery() const
{
    return m_searchQuery;
}

int DocumentSession::matchCount() const
{
    return m_totalMatchCount;
}

QString DocumentSession::matchCountDisplay() const
{
    const int highlightLimit = runtimeTuning().searchHighlightLimit;
    if (m_totalMatchCount <= 0) {
        return QStringLiteral("0");
    }
    if (m_matchOverflow) {
        return QStringLiteral("%1+").arg(highlightLimit);
    }
    return QString::number(m_totalMatchCount);
}

int DocumentSession::currentMatch() const
{
    if (m_currentMatchIndex < 0 || m_matchPositions.isEmpty()) {
        return 0;
    }
    return m_currentMatchIndex + 1;
}

bool DocumentSession::replaceAllEnabled() const
{
    const int replaceAllLimit = runtimeTuning().replaceAllLimit;
    if (searching()) {
        return false;
    }

    return !m_saving
        && m_totalMatchCount > 0
        && !m_matchOverflow
        && m_totalMatchCount <= replaceAllLimit;
}

bool DocumentSession::searching() const
{
    if (m_searchQueued) {
        return true;
    }

    if (!m_searchRunning) {
        return false;
    }

    return m_runningSearchRequestId == m_searchRequestId;
}

bool DocumentSession::loadFromFile(const QString &path, QString *errorMessage)
{
    if (m_saving) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存进行中，暂时无法加载新文件。");
        }
        emit operationBlocked(QStringLiteral("保存进行中，暂时无法加载新文件。"));
        return false;
    }

    const DecodedFileResult result = decodeFileForLoad(path);
    if (!result.ok) {
        if (errorMessage) {
            *errorMessage = result.error;
        }
        return false;
    }

    applyLoadedFile(result.path, result.text, result.codec);
    return true;
}

DocumentSession::DecodedFileResult DocumentSession::decodeFileForLoad(const QString &path)
{
    DecodedFileResult result;
    result.path = path;
    const RuntimeTuning &tuning = runtimeTuning();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("打开文件失败: %1").arg(file.errorString());
        return result;
    }

    const qint64 fileBytes = file.size();
    if (fileBytes > tuning.maxOpenFileBytes) {
        result.error = QStringLiteral("文件过大（%1 MB），超过支持上限（%2 MB）。")
                           .arg(fileBytes / (1024ll * 1024ll))
                           .arg(tuning.maxOpenFileBytes / (1024ll * 1024ll));
        file.close();
        return result;
    }

    QString decoded;
    QString codecUsed;
    bool decodedOk = false;

    try {
        if (fileBytes >= tuning.mappedDecodeThresholdBytes
            && fileBytes <= static_cast<qint64>(std::numeric_limits<int>::max())) {
            uchar *mapped = file.map(0, fileBytes);
            if (mapped) {
                const QByteArray mappedBytes = QByteArray::fromRawData(
                    reinterpret_cast<const char *>(mapped),
                    static_cast<int>(fileBytes));
                decodedOk = decodeText(mappedBytes, &decoded, &codecUsed);
                file.unmap(mapped);
            }
        }

        if (!decodedOk) {
            const QByteArray bytes = file.readAll();
            decodedOk = decodeText(bytes, &decoded, &codecUsed);
        }
    } catch (const std::bad_alloc &) {
        file.close();
        result.error = QStringLiteral("打开文件失败：内存不足。");
        return result;
    } catch (const std::exception &ex) {
        file.close();
        result.error = QStringLiteral("打开文件失败：%1").arg(QString::fromUtf8(ex.what()));
        return result;
    } catch (...) {
        file.close();
        result.error = QStringLiteral("打开文件失败：未知异常。");
        return result;
    }

    file.close();

    if (!decodedOk) {
        result.error = QStringLiteral("文件解码失败。");
        return result;
    }

    if (decoded.length() > tuning.maxDocumentChars) {
        result.error = QStringLiteral("文件内容过大，超过编辑器限制。");
        return result;
    }

    result.ok = true;
    result.text = decoded;
    result.codec = codecUsed;
    return result;
}

void DocumentSession::applyLoadedFile(const QString &path, const QString &decodedText, const QString &codecName)
{
    const int previousLineCount = lineCount();

    m_buffer.reset(decodedText);
    m_lineIndex.rebuild(decodedText);
    m_fullTextCacheEnabled = decodedText.length() <= kFullTextCacheThresholdChars;
    if (m_fullTextCacheEnabled) {
        m_cachedText = decodedText;
    } else {
        m_cachedText.clear();
    }
    m_codecName = codecName.isEmpty() ? QStringLiteral("UTF-8") : codecName;
    m_searchQuery.clear();
    m_matchPositions.clear();
    m_matchOverflow = false;
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;
    ++m_contentRevision;
    ++m_searchRequestId;
    m_searchQueued = false;
    m_queuedSearchQuery.clear();
    clearEditHistory();
    m_cursorPosition = 0;
    m_currentLine = 1;

    setFilePathInternal(path);
    setDirtyInternal(false);
    m_stateId = 0;
    m_savedStateId = 0;
    m_nextStateId = 1;

    if (lineCount() != previousLineCount) {
        emit lineCountChanged();
    }

    emit codecChanged();
    emit textChanged();
    emit currentLineChanged();
    emit searchStateChanged();
    emit editCapabilitiesChanged();
}

bool DocumentSession::saveSync(QString *errorMessage)
{
    if (m_filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("当前文档没有文件路径，请使用另存为。");
        }
        return false;
    }
    return saveAsSync(m_filePath, errorMessage);
}

bool DocumentSession::saveAsSync(const QString &path, QString *errorMessage)
{
    if (m_saving) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("保存进行中，暂时不可重复保存。");
        }
        emit operationBlocked(QStringLiteral("保存进行中，暂时不可重复保存。"));
        return false;
    }

    m_saving = true;
    emit savingChanged();
    emit editCapabilitiesChanged();

    const bool ok = saveToPathSync(path, errorMessage);

    m_saving = false;
    emit savingChanged();
    emit editCapabilitiesChanged();

    if (ok) {
        setFilePathInternal(path);
        setDirtyInternal(false);
        m_savedStateId = m_stateId;
    }

    return ok;
}

void DocumentSession::saveAsync()
{
    if (m_filePath.isEmpty()) {
        emit operationBlocked(QStringLiteral("当前文档没有文件路径，请使用另存为。"));
        emit saveFinished(false, QStringLiteral("当前文档没有文件路径，请使用另存为。"));
        return;
    }

    beginAsyncSave(m_filePath);
}

void DocumentSession::saveAsAsync(const QString &path)
{
    beginAsyncSave(path);
}

bool DocumentSession::canModify() const
{
    return !m_saving;
}

bool DocumentSession::setTextFromEditor(const QString &newText)
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    const QString oldText = currentTextSnapshot();
    if (oldText == newText) {
        return true;
    }

    const int oldLength = oldText.length();
    const int newLength = newText.length();
    const int minLength = std::min(oldLength, newLength);

    int prefix = 0;
    while (prefix < minLength && oldText.at(prefix) == newText.at(prefix)) {
        ++prefix;
    }

    int suffix = 0;
    while (suffix < (oldLength - prefix)
        && suffix < (newLength - prefix)
        && oldText.at(oldLength - 1 - suffix) == newText.at(newLength - 1 - suffix)) {
        ++suffix;
    }

    const int removeLength = oldLength - prefix - suffix;
    const QString inserted = newText.mid(prefix, newLength - prefix - suffix);
    return applyEditInternal(prefix, removeLength, inserted);
}

int DocumentSession::applyTextEdit(int position, int removeLength, const QString &insertedText)
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return -1;
    }

    if (!applyEditInternal(position, removeLength, insertedText)) {
        return -1;
    }

    m_cursorPosition = qBound(0, position + insertedText.length(), m_buffer.length());
    updateCurrentLineFromCursor();
    return m_cursorPosition;
}

void DocumentSession::forceSetText(const QString &newText)
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return;
    }

    if (newText.length() > runtimeTuning().maxDocumentChars) {
        emit operationBlocked(QStringLiteral("文件内容过大，已超过编辑器内存限制。"));
        return;
    }

    if (currentTextSnapshot() == newText) {
        return;
    }

    const int previousLineCount = lineCount();
    m_buffer.reset(newText);
    m_lineIndex.rebuild(newText);
    m_fullTextCacheEnabled = newText.length() <= kFullTextCacheThresholdChars;
    if (m_fullTextCacheEnabled) {
        m_cachedText = newText;
    } else {
        m_cachedText.clear();
    }
    setDirtyInternal(true);

    if (lineCount() != previousLineCount) {
        emit lineCountChanged();
    }

    updateCurrentLineFromCursor();
    clearEditHistory();
    m_stateId = m_nextStateId++;
    setDirtyInternal(m_stateId != m_savedStateId);
    ++m_contentRevision;
    rebuildSearchCache();
    emit textChanged();
}

void DocumentSession::setCursorPosition(int position)
{
    const int clamped = qBound(0, position, m_buffer.length());
    if (clamped == m_cursorPosition) {
        return;
    }

    m_cursorPosition = clamped;
    updateCurrentLineFromCursor();
}

void DocumentSession::setSearchQuery(const QString &query)
{
    if (m_searchQuery == query) {
        return;
    }

    m_searchQuery = query;
    rebuildSearchCache();
}

int DocumentSession::findNext()
{
    if (searching()) {
        emit operationBlocked(QStringLiteral("正在查找，请稍候。"));
        return -1;
    }

    if (m_matchPositions.isEmpty()) {
        return -1;
    }

    if (m_currentMatchIndex < 0) {
        m_currentMatchIndex = 0;
    } else {
        m_currentMatchIndex = (m_currentMatchIndex + 1) % m_matchPositions.size();
    }

    emit searchStateChanged();
    return m_matchPositions.at(m_currentMatchIndex);
}

int DocumentSession::findPrevious()
{
    if (searching()) {
        emit operationBlocked(QStringLiteral("正在查找，请稍候。"));
        return -1;
    }

    if (m_matchPositions.isEmpty()) {
        return -1;
    }

    if (m_currentMatchIndex < 0) {
        m_currentMatchIndex = 0;
    } else {
        m_currentMatchIndex = (m_currentMatchIndex - 1 + m_matchPositions.size()) % m_matchPositions.size();
    }

    emit searchStateChanged();
    return m_matchPositions.at(m_currentMatchIndex);
}

int DocumentSession::currentMatchPosition() const
{
    if (m_currentMatchIndex < 0 || m_currentMatchIndex >= m_matchPositions.size()) {
        return -1;
    }
    return m_matchPositions.at(m_currentMatchIndex);
}

int DocumentSession::queryLength() const
{
    return m_searchQuery.length();
}

QVector<int> DocumentSession::searchMatchPositionsInRange(int start, int endExclusive, int maxCount) const
{
    QVector<int> result;
    if (maxCount <= 0 || m_matchPositions.isEmpty() || m_searchQuery.isEmpty()) {
        return result;
    }

    const int safeStart = qMax(0, start);
    const int safeEnd = qMax(safeStart, endExclusive);
    if (safeStart >= safeEnd) {
        return result;
    }

    result.reserve(qMin(maxCount, m_matchPositions.size()));

    auto it = std::lower_bound(m_matchPositions.cbegin(), m_matchPositions.cend(), safeStart);
    for (; it != m_matchPositions.cend() && *it < safeEnd; ++it) {
        result.push_back(*it);
        if (result.size() >= maxCount) {
            break;
        }
    }

    return result;
}

bool DocumentSession::replaceCurrent(const QString &replacement)
{
    if (searching()) {
        emit operationBlocked(QStringLiteral("正在查找，请稍候。"));
        return false;
    }

    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    if (m_searchQuery.isEmpty() || m_matchPositions.isEmpty()) {
        return false;
    }

    if (m_currentMatchIndex < 0 || m_currentMatchIndex >= m_matchPositions.size()) {
        m_currentMatchIndex = 0;
    }

    const int position = m_matchPositions.at(m_currentMatchIndex);
    if (!applyEditInternal(position, m_searchQuery.length(), replacement)) {
        return false;
    }

    m_cursorPosition = qBound(0, position + replacement.length(), m_buffer.length());
    updateCurrentLineFromCursor();
    return true;
}

int DocumentSession::replaceAll(const QString &replacement)
{
    if (searching()) {
        emit operationBlocked(QStringLiteral("正在查找，请稍候。"));
        return 0;
    }

    if (!replaceAllEnabled()) {
        return 0;
    }

    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return 0;
    }

    const int queryLen = m_searchQuery.length();
    if (queryLen <= 0 || m_matchPositions.isEmpty()) {
        return 0;
    }

    const QVector<int> matches = m_matchPositions;
    const QString source = currentTextSnapshot();
    const int sourceLen = source.length();
    const qint64 expectedLength = static_cast<qint64>(sourceLen)
        + static_cast<qint64>(matches.size()) * static_cast<qint64>(replacement.length() - queryLen);
    if (expectedLength > runtimeTuning().maxDocumentChars) {
        emit operationBlocked(QStringLiteral("替换后内容过大，已超过编辑器限制。"));
        return 0;
    }

    QString updated;
    const int expectedDelta = matches.size() * (replacement.length() - queryLen);
    updated.reserve(qMax(0, sourceLen + expectedDelta));

    int replaced = 0;
    int cursor = 0;
    for (int i = 0; i < matches.size(); ++i) {
        const int pos = matches.at(i);
        if (pos < cursor || pos < 0 || pos + queryLen > sourceLen) {
            continue;
        }
        if (source.midRef(pos, queryLen) != m_searchQuery) {
            continue;
        }

        updated.append(source.mid(cursor, pos - cursor));
        updated.append(replacement);
        cursor = pos + queryLen;
        ++replaced;
    }
    updated.append(source.mid(cursor));

    if (replaced <= 0) {
        return 0;
    }

    const int previousLineCount = lineCount();
    m_buffer.reset(updated);
    m_lineIndex.rebuild(updated);
    m_fullTextCacheEnabled = updated.length() <= kFullTextCacheThresholdChars;
    if (m_fullTextCacheEnabled) {
        m_cachedText = updated;
    } else {
        m_cachedText.clear();
    }

    setDirtyInternal(true);

    if (lineCount() != previousLineCount) {
        emit lineCountChanged();
    }

    m_cursorPosition = qBound(0, m_cursorPosition, m_buffer.length());
    updateCurrentLineFromCursor();
    clearEditHistory();
    m_stateId = m_nextStateId++;
    setDirtyInternal(m_stateId != m_savedStateId);
    ++m_contentRevision;
    rebuildSearchCache();
    emit textChanged();

    return replaced;
}

bool DocumentSession::saveToPathSync(const QString &path, QString *errorMessage)
{
    SaveResult result;
    try {
        result = writeFile(path, m_buffer, m_codecName);
    } catch (const std::exception &ex) {
        result.ok = false;
        result.targetPath = path;
        result.message = QStringLiteral("保存失败：内存不足或数据异常（%1）")
                             .arg(QString::fromUtf8(ex.what()));
    } catch (...) {
        result.ok = false;
        result.targetPath = path;
        result.message = QStringLiteral("保存失败：发生未知异常。");
    }

    if (!result.ok) {
        if (errorMessage) {
            *errorMessage = result.message;
        }
        return false;
    }

    return true;
}

void DocumentSession::beginAsyncSave(const QString &targetPath)
{
    if (m_saving) {
        emit operationBlocked(QStringLiteral("保存进行中，暂时不可重复保存。"));
        return;
    }

    if (targetPath.isEmpty()) {
        emit operationBlocked(QStringLiteral("保存路径为空，无法保存。"));
        emit saveFinished(false, QStringLiteral("保存路径为空，无法保存。"));
        return;
    }

    m_saving = true;
    emit savingChanged();
    emit editCapabilitiesChanged();

    PieceTableBuffer::SaveSnapshot snapshot;
    try {
        snapshot = m_buffer.makeSaveSnapshot();
    } catch (const std::exception &ex) {
        SaveResult fallbackResult;
        try {
            fallbackResult = writeFile(targetPath, m_buffer, m_codecName);
        } catch (const std::exception &fallbackEx) {
            fallbackResult.ok = false;
            fallbackResult.targetPath = targetPath;
            fallbackResult.message = QStringLiteral("保存失败：%1").arg(QString::fromUtf8(fallbackEx.what()));
        } catch (...) {
            fallbackResult.ok = false;
            fallbackResult.targetPath = targetPath;
            fallbackResult.message = QStringLiteral("保存失败：未知异常。");
        }

        m_saving = false;
        emit savingChanged();
        emit editCapabilitiesChanged();
        if (fallbackResult.ok) {
            setFilePathInternal(fallbackResult.targetPath);
            setDirtyInternal(false);
            m_savedStateId = m_stateId;
            emit saveFinished(true, QStringLiteral("保存成功"));
            return;
        }

        const QString msg =
            QStringLiteral("保存失败：内存不足或数据异常（%1）").arg(QString::fromUtf8(ex.what()));
        emit operationBlocked(msg);
        emit saveFinished(false, fallbackResult.message.isEmpty() ? msg : fallbackResult.message);
        return;
    } catch (...) {
        m_saving = false;
        emit savingChanged();
        emit editCapabilitiesChanged();
        const QString msg = QStringLiteral("保存失败：发生未知异常。");
        emit operationBlocked(msg);
        emit saveFinished(false, msg);
        return;
    }
    const QString snapshotCodec = m_codecName;

    auto *watcher = new QFutureWatcher<SaveResult>(this);
    m_saveWatcher = watcher;

    connect(watcher, &QFutureWatcher<SaveResult>::finished, this, [this, watcher, targetPath]() {
        SaveResult result;
        try {
            result = watcher->result();
        } catch (const std::exception &ex) {
            result.ok = false;
            result.targetPath = targetPath;
            result.message = QStringLiteral("保存线程异常：%1").arg(QString::fromUtf8(ex.what()));
        } catch (...) {
            result.ok = false;
            result.targetPath = targetPath;
            result.message = QStringLiteral("保存线程异常：未知错误。");
        }
        watcher->deleteLater();
        if (m_saveWatcher == watcher) {
            m_saveWatcher = nullptr;
        }

        if (!result.ok && result.message.contains(QStringLiteral("内存不足"))) {
            SaveResult retryResult;
            try {
                retryResult = writeFile(targetPath, m_buffer, m_codecName);
            } catch (const std::exception &ex) {
                retryResult.ok = false;
                retryResult.targetPath = targetPath;
                retryResult.message = QStringLiteral("重试保存失败：%1").arg(QString::fromUtf8(ex.what()));
            } catch (...) {
                retryResult.ok = false;
                retryResult.targetPath = targetPath;
                retryResult.message = QStringLiteral("重试保存失败：未知异常。");
            }

            if (retryResult.ok) {
                result = retryResult;
            } else if (!retryResult.message.isEmpty()) {
                result.message = result.message + QStringLiteral("；") + retryResult.message;
            }
        }

        if (result.ok) {
            m_saving = false;
            emit savingChanged();
            emit editCapabilitiesChanged();
            setFilePathInternal(result.targetPath);
            setDirtyInternal(false);
            m_savedStateId = m_stateId;
            emit saveFinished(true, QStringLiteral("保存成功"));
            return;
        }

        m_saving = false;
        emit savingChanged();
        emit editCapabilitiesChanged();
        emit saveFinished(false, result.message);
    });

    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot), snapshotCodec, targetPath]() {
        try {
            return writeFile(targetPath, snapshot, snapshotCodec);
        } catch (const std::exception &ex) {
            SaveResult r;
            r.ok = false;
            r.targetPath = targetPath;
            r.message = QStringLiteral("保存失败：%1").arg(QString::fromUtf8(ex.what()));
            return r;
        } catch (...) {
            SaveResult r;
            r.ok = false;
            r.targetPath = targetPath;
            r.message = QStringLiteral("保存失败：未知异常。");
            return r;
        }
    }));
}

bool DocumentSession::applyEditInternal(int position, int removeLength, const QString &insertedText, bool recordHistory)
{
    try {
        position = qBound(0, position, m_buffer.length());
        removeLength = qBound(0, removeLength, m_buffer.length() - position);

        const qint64 newLength = static_cast<qint64>(m_buffer.length())
            - static_cast<qint64>(removeLength)
            + static_cast<qint64>(insertedText.length());
        if (newLength > runtimeTuning().maxDocumentChars) {
            emit operationBlocked(QStringLiteral("编辑后内容过大，已超过编辑器限制。"));
            return false;
        }

        if (removeLength == 0 && insertedText.isEmpty()) {
            return true;
        }

        EditCommand cmd;
        if (recordHistory) {
            cmd.position = position;
            cmd.cursorBefore = m_cursorPosition;
            cmd.insertedText = insertedText;
            cmd.fromState = m_stateId;
            if (removeLength > 0) {
                cmd.removedText = m_buffer.mid(position, removeLength);
            }
        }

        const int previousLineCount = lineCount();

        if (removeLength > 0) {
            m_buffer.remove(position, removeLength);
            if (m_fullTextCacheEnabled) {
                m_cachedText.remove(position, removeLength);
            }
            m_lineIndex.applyDelete(position, removeLength);
        }

        if (!insertedText.isEmpty()) {
            m_buffer.insert(position, insertedText);
            if (m_fullTextCacheEnabled) {
                m_cachedText.insert(position, insertedText);
            }
            m_lineIndex.applyInsert(position, insertedText);
        }

        const bool shouldCacheFullText = m_buffer.length() <= kFullTextCacheThresholdChars;
        if (shouldCacheFullText != m_fullTextCacheEnabled) {
            m_fullTextCacheEnabled = shouldCacheFullText;
            if (m_fullTextCacheEnabled) {
                m_cachedText = m_buffer.toString();
            } else {
                m_cachedText.clear();
            }
        }

        if (lineCount() != previousLineCount) {
            emit lineCountChanged();
        }

        m_cursorPosition = qBound(0, m_cursorPosition, m_buffer.length());
        updateCurrentLineFromCursor();

        if (recordHistory) {
            cmd.toState = m_nextStateId++;
            cmd.cursorAfter = qBound(0, position + insertedText.length(), m_buffer.length());
            m_stateId = cmd.toState;
            m_redoHistory.clear();
            m_redoBytes = 0;
            if (!pushUndoCommand(cmd)) {
                clearEditHistory();
                emit operationBlocked(QStringLiteral("本次编辑内容较大，已清空撤销历史。"));
            }
            setDirtyInternal(m_stateId != m_savedStateId);
        }

        ++m_contentRevision;
        const bool hasSearchWork =
            !m_searchQuery.isEmpty()
            || m_searchQueued
            || m_searchRunning
            || !m_matchPositions.isEmpty()
            || m_matchOverflow
            || m_totalMatchCount != 0
            || m_currentMatchIndex != -1;
        if (hasSearchWork) {
            rebuildSearchCache();
        }
        emit textChanged();
        emit editCapabilitiesChanged();
        return true;
    } catch (const std::bad_alloc &) {
        recoverAfterEditFailure();
        emit operationBlocked(QStringLiteral("编辑失败：内存不足。已尝试恢复文档状态。"));
        return false;
    } catch (const std::exception &ex) {
        recoverAfterEditFailure();
        emit operationBlocked(QStringLiteral("编辑失败：%1。已尝试恢复文档状态。")
                                  .arg(QString::fromUtf8(ex.what())));
        return false;
    } catch (...) {
        recoverAfterEditFailure();
        emit operationBlocked(QStringLiteral("编辑失败：未知异常。已尝试恢复文档状态。"));
        return false;
    }
}

void DocumentSession::recoverAfterEditFailure()
{
    try {
        const QString snapshot = m_buffer.toString();
        m_lineIndex.rebuild(snapshot);
        const bool shouldCacheFullText = m_buffer.length() <= kFullTextCacheThresholdChars;
        m_fullTextCacheEnabled = shouldCacheFullText;
        if (shouldCacheFullText) {
            m_cachedText = snapshot;
        } else {
            m_cachedText.clear();
        }
    } catch (...) {
        m_fullTextCacheEnabled = false;
        m_cachedText.clear();
    }

    m_cursorPosition = qBound(0, m_cursorPosition, m_buffer.length());
    updateCurrentLineFromCursor();

    clearEditHistory();
    m_stateId = m_nextStateId++;
    setDirtyInternal(m_stateId != m_savedStateId);

    ++m_contentRevision;
    ++m_searchRequestId;
    if (m_latestSearchRequestId) {
        m_latestSearchRequestId->store(m_searchRequestId, std::memory_order_relaxed);
    }
    m_searchQueued = false;
    m_searchRunning = false;
    m_runningSearchRequestId = 0;
    m_queuedSearchQuery.clear();
    m_matchPositions.clear();
    m_matchOverflow = false;
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;

    emit lineCountChanged();
    emit textChanged();
    emit searchStateChanged();
}

void DocumentSession::updateCurrentLineFromCursor()
{
    const int line = m_lineIndex.lineForOffset(m_cursorPosition) + 1;
    if (line != m_currentLine) {
        m_currentLine = line;
        emit currentLineChanged();
    }
}

void DocumentSession::rebuildSearchCache()
{
    if (m_searchQuery.isEmpty()) {
        const bool hadSearchState =
            m_searchQueued
            || m_searchRunning
            || !m_matchPositions.isEmpty()
            || m_matchOverflow
            || m_totalMatchCount != 0
            || m_currentMatchIndex != -1;

        ++m_searchRequestId;
        if (m_latestSearchRequestId) {
            m_latestSearchRequestId->store(m_searchRequestId, std::memory_order_relaxed);
        }
        m_matchPositions.clear();
        m_matchOverflow = false;
        m_totalMatchCount = 0;
        m_currentMatchIndex = -1;
        m_searchQueued = false;
        m_queuedSearchQuery.clear();
        if (hadSearchState) {
            emit searchStateChanged();
        }
        return;
    }

    ++m_searchRequestId;
    const quint64 requestId = m_searchRequestId;
    if (m_latestSearchRequestId) {
        m_latestSearchRequestId->store(requestId, std::memory_order_relaxed);
    }

    m_matchPositions.clear();
    m_matchOverflow = false;
    m_totalMatchCount = 0;
    m_currentMatchIndex = -1;

    const int totalLength = m_buffer.length();
    const RuntimeTuning &tuning = runtimeTuning();
    if (totalLength < tuning.asyncSearchThresholdChars) {
        const QString text = currentTextSnapshot();
        applySearchResult(computeSearch(text,
                                        m_searchQuery,
                                        requestId,
                                        m_contentRevision,
                                        m_latestSearchRequestId.get()));
        return;
    }

    m_queuedSearchRequestId = requestId;
    m_queuedSearchRevision = m_contentRevision;
    m_queuedSearchQuery = m_searchQuery;
    m_searchQueued = true;
    const int debounceMs =
        (totalLength >= tuning.largeSearchDebounceThresholdChars)
            ? tuning.searchDebounceLargeMs
            : tuning.searchDebounceMs;

    if (m_searchRunning) {
        m_searchDebounceTimer.start(debounceMs);
        emit searchStateChanged();
        return;
    }

    m_searchDebounceTimer.start(debounceMs);
    emit searchStateChanged();
}

void DocumentSession::startQueuedSearch()
{
    if (!m_searchQueued || m_searchRunning || m_queuedSearchQuery.isEmpty()) {
        return;
    }

    const quint64 requestId = m_queuedSearchRequestId;
    const quint64 revision = m_queuedSearchRevision;
    const QString query = m_queuedSearchQuery;
    const std::shared_ptr<std::atomic<quint64>> latestRequestId = m_latestSearchRequestId;
    const bool useSnapshotSearch = !m_fullTextCacheEnabled;
    PieceTableBuffer::SaveSnapshot snapshot;
    QString text;
    try {
        if (useSnapshotSearch) {
            snapshot = m_buffer.makeSaveSnapshot();
        } else {
            text = currentTextSnapshot();
        }
    } catch (...) {
        m_searchQueued = false;
        emit searchStateChanged();
        return;
    }

    m_searchQueued = false;
    m_searchRunning = true;
    m_runningSearchRequestId = requestId;
    emit searchStateChanged();

    auto *watcher = new QFutureWatcher<SearchComputeResult>(this);
    m_searchWatcher = watcher;

    connect(watcher, &QFutureWatcher<SearchComputeResult>::finished, this, [this, watcher]() {
        SearchComputeResult result;
        bool hasResult = false;
        try {
            result = watcher->result();
            hasResult = true;
        } catch (...) {
            hasResult = false;
        }
        watcher->deleteLater();
        if (m_searchWatcher == watcher) {
            m_searchWatcher = nullptr;
        }

        m_searchRunning = false;
        m_runningSearchRequestId = 0;
        emit searchStateChanged();
        if (hasResult) {
            applySearchResult(result);
        }

        if (m_searchQueued) {
            startQueuedSearch();
        }
    });

    if (useSnapshotSearch) {
        watcher->setFuture(QtConcurrent::run(
            [snapshot = std::move(snapshot), query, requestId, revision, latestRequestId]() {
                return computeSearch(snapshot, query, requestId, revision, latestRequestId.get());
            }));
    } else {
        watcher->setFuture(QtConcurrent::run([text, query, requestId, revision, latestRequestId]() {
            return computeSearch(text, query, requestId, revision, latestRequestId.get());
        }));
    }
}

void DocumentSession::applySearchResult(const SearchComputeResult &result)
{
    if (result.requestId != m_searchRequestId) {
        return;
    }
    if (result.contentRevision != m_contentRevision) {
        return;
    }
    if (result.query != m_searchQuery) {
        return;
    }

    m_matchPositions = result.positions;
    m_matchOverflow = result.overflow;
    m_totalMatchCount = result.total;
    m_currentMatchIndex = m_matchPositions.isEmpty() ? -1 : 0;
    emit searchStateChanged();
}

DocumentSession::SearchComputeResult DocumentSession::computeSearch(const QString &text,
                                                                    const QString &query,
                                                                    quint64 requestId,
                                                                    quint64 contentRevision,
                                                                    const std::atomic<quint64> *latestRequestId)
{
    const int highlightLimit = runtimeTuning().searchHighlightLimit;
    SearchComputeResult result;
    result.requestId = requestId;
    result.contentRevision = contentRevision;
    result.query = query;

    if (query.isEmpty()) {
        return result;
    }

    const int queryLen = query.length();
    if (queryLen <= 0) {
        return result;
    }

    int from = 0;
    while (from <= text.length()) {
        if (latestRequestId
            && latestRequestId->load(std::memory_order_relaxed) != requestId) {
            break;
        }

        const int pos = text.indexOf(query, from, Qt::CaseSensitive);
        if (pos < 0) {
            break;
        }

        ++result.total;
        if (result.positions.size() < highlightLimit) {
            result.positions.push_back(pos);
        }

        if (result.total > highlightLimit) {
            result.overflow = true;
            break;
        }

        from = pos + qMax(1, queryLen);
    }

    return result;
}

DocumentSession::SearchComputeResult DocumentSession::computeSearch(
    const PieceTableBuffer::SaveSnapshot &snapshot,
    const QString &query,
    quint64 requestId,
    quint64 contentRevision,
    const std::atomic<quint64> *latestRequestId)
{
    const int highlightLimit = runtimeTuning().searchHighlightLimit;
    SearchComputeResult result;
    result.requestId = requestId;
    result.contentRevision = contentRevision;
    result.query = query;

    const int queryLen = query.length();
    if (queryLen <= 0) {
        return result;
    }

    QVector<int> lps(queryLen, 0);
    for (int i = 1, len = 0; i < queryLen;) {
        if (query.at(i) == query.at(len)) {
            lps[i++] = ++len;
        } else if (len > 0) {
            len = lps.at(len - 1);
        } else {
            lps[i++] = 0;
        }
    }

    int matched = 0;
    int globalOffset = 0;
    int checkCounter = 0;

    for (const PieceTableBuffer::SnapshotSegment &segment : snapshot.segments) {
        if (segment.length <= 0) {
            continue;
        }

        const QString &source = segment.fromAddBuffer ? snapshot.add : snapshot.original;
        const int start = qMax(0, segment.start);
        const int end = qMin(source.length(), segment.start + segment.length);
        for (int i = start; i < end; ++i, ++globalOffset) {
            if (((++checkCounter) & 0x3FFF) == 0) {
                if (latestRequestId
                    && latestRequestId->load(std::memory_order_relaxed) != requestId) {
                    return result;
                }
            }

            const QChar ch = source.at(i);
            while (matched > 0 && ch != query.at(matched)) {
                matched = lps.at(matched - 1);
            }
            if (ch == query.at(matched)) {
                ++matched;
            }

            if (matched == queryLen) {
                const int pos = globalOffset - queryLen + 1;
                ++result.total;
                if (result.positions.size() < highlightLimit) {
                    result.positions.push_back(pos);
                }
                if (result.total > highlightLimit) {
                    result.overflow = true;
                    return result;
                }
                // Keep behavior aligned with old indexOf loop: non-overlapping matches.
                matched = 0;
            }
        }
    }

    return result;
}

void DocumentSession::setDirtyInternal(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }

    m_dirty = dirty;
    emit dirtyChanged();
}

void DocumentSession::setFilePathInternal(const QString &path)
{
    if (m_filePath == path) {
        return;
    }

    m_filePath = path;
    emit filePathChanged();
}

int DocumentSession::commandBytes(const EditCommand &cmd) const
{
    return (cmd.removedText.size() + cmd.insertedText.size()) * static_cast<int>(sizeof(QChar))
        + static_cast<int>(sizeof(EditCommand));
}

void DocumentSession::clearEditHistory()
{
    m_undoHistory.clear();
    m_redoHistory.clear();
    m_undoBytes = 0;
    m_redoBytes = 0;
    emit editCapabilitiesChanged();
}

void DocumentSession::trimHistoryByLimit(QVector<EditCommand> &history, int &bytes)
{
    while (!history.isEmpty()
           && (bytes > kUndoHistoryMaxBytes || history.size() > kUndoHistoryMaxCommands)) {
        const EditCommand cmd = history.takeFirst();
        bytes -= commandBytes(cmd);
    }
    if (bytes < 0) {
        bytes = 0;
    }
}

bool DocumentSession::pushUndoCommand(const EditCommand &cmd)
{
    const int bytes = commandBytes(cmd);
    if (bytes > kUndoEntryMaxBytes) {
        return false;
    }

    m_undoHistory.push_back(cmd);
    m_undoBytes += bytes;
    trimHistoryByLimit(m_undoHistory, m_undoBytes);
    return true;
}

bool DocumentSession::pushRedoCommand(const EditCommand &cmd)
{
    const int bytes = commandBytes(cmd);
    if (bytes > kUndoEntryMaxBytes) {
        return false;
    }

    m_redoHistory.push_back(cmd);
    m_redoBytes += bytes;
    trimHistoryByLimit(m_redoHistory, m_redoBytes);
    return true;
}

bool DocumentSession::decodeText(const QByteArray &bytes, QString *decoded, QString *codecUsed)
{
    static const QByteArray kBomUtf32Be("\x00\x00\xFE\xFF", 4);
    static const QByteArray kBomUtf32Le("\xFF\xFE\x00\x00", 4);
    static const QByteArray kBomUtf8("\xEF\xBB\xBF", 3);
    static const QByteArray kBomUtf16Le("\xFF\xFE", 2);
    static const QByteArray kBomUtf16Be("\xFE\xFF", 2);

    if (bytes.startsWith(kBomUtf32Be)) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-32BE");
        if (!codec) {
            return false;
        }
        *codecUsed = QStringLiteral("UTF-32BE");
        *decoded = codec->toUnicode(bytes.constData() + 4, bytes.size() - 4);
        return true;
    }

    if (bytes.startsWith(kBomUtf32Le)) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-32LE");
        if (!codec) {
            return false;
        }
        *codecUsed = QStringLiteral("UTF-32LE");
        *decoded = codec->toUnicode(bytes.constData() + 4, bytes.size() - 4);
        return true;
    }

    if (bytes.startsWith(kBomUtf8)) {
        *codecUsed = QStringLiteral("UTF-8");
        *decoded = QString::fromUtf8(bytes.constData() + 3, bytes.size() - 3);
        return true;
    }

    if (bytes.startsWith(kBomUtf16Le)) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        if (!codec) {
            return false;
        }
        *codecUsed = QStringLiteral("UTF-16LE");
        *decoded = codec->toUnicode(bytes.constData() + 2, bytes.size() - 2);
        return true;
    }

    if (bytes.startsWith(kBomUtf16Be)) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16BE");
        if (!codec) {
            return false;
        }
        *codecUsed = QStringLiteral("UTF-16BE");
        *decoded = codec->toUnicode(bytes.constData() + 2, bytes.size() - 2);
        return true;
    }

    const Utf16Heuristic utf16Hint = detectUtf16WithoutBom(bytes);
    if (utf16Hint == Utf16Heuristic::LittleEndian) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        if (!codec) {
            return false;
        }
        *codecUsed = QStringLiteral("UTF-16LE");
        *decoded = codec->toUnicode(bytes);
        return true;
    }
    if (utf16Hint == Utf16Heuristic::BigEndian) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16BE");
        if (!codec) {
            return false;
        }
        *codecUsed = QStringLiteral("UTF-16BE");
        *decoded = codec->toUnicode(bytes);
        return true;
    }

    if (isValidUtf8(bytes)) {
        *codecUsed = QStringLiteral("UTF-8");
        *decoded = QString::fromUtf8(bytes);
        return true;
    }

    QTextCodec *gbCodec = QTextCodec::codecForName("GB18030");
    if (!gbCodec) {
        gbCodec = QTextCodec::codecForLocale();
    }

    if (!gbCodec) {
        return false;
    }

    *codecUsed = QString::fromLatin1(gbCodec->name());
    *decoded = gbCodec->toUnicode(bytes);
    return true;
}

bool DocumentSession::isValidUtf8(const QByteArray &bytes)
{
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    const int size = bytes.size();
    int i = 0;

    while (i < size) {
        const unsigned char c = data[i++];
        if (c <= 0x7F) {
            continue;
        }

        int trail = 0;
        uint codePoint = 0;

        if ((c & 0xE0) == 0xC0) {
            // 2-byte sequence: 0xC2..0xDF are valid starts.
            if (c < 0xC2) {
                return false;
            }
            trail = 1;
            codePoint = static_cast<uint>(c & 0x1F);
        } else if ((c & 0xF0) == 0xE0) {
            trail = 2;
            codePoint = static_cast<uint>(c & 0x0F);
        } else if ((c & 0xF8) == 0xF0) {
            // 4-byte sequence: max valid leading byte is 0xF4.
            if (c > 0xF4) {
                return false;
            }
            trail = 3;
            codePoint = static_cast<uint>(c & 0x07);
        } else {
            return false;
        }

        if (i + trail > size) {
            return false;
        }

        for (int t = 0; t < trail; ++t) {
            const unsigned char next = data[i++];
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | static_cast<uint>(next & 0x3F);
        }

        if (trail == 1) {
            if (codePoint < 0x80) {
                return false;
            }
        } else if (trail == 2) {
            if (codePoint < 0x800) {
                return false;
            }
            if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                return false;
            }
        } else {
            if (codePoint < 0x10000 || codePoint > 0x10FFFF) {
                return false;
            }
        }
    }

    return true;
}

DocumentSession::SaveResult DocumentSession::writeFile(const QString &targetPath,
                                                       const PieceTableBuffer &buffer,
                                                       const QString &codecName)
{
    SaveResult result;
    result.targetPath = targetPath;

    QTextCodec *codec = QTextCodec::codecForName(codecName.toUtf8());
    if (!codec) {
        codec = QTextCodec::codecForName("UTF-8");
    }

    if (!codec) {
        result.ok = false;
        result.message = QStringLiteral("编码器不可用，无法保存文件。");
        return result;
    }

    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.ok = false;
        result.message = QStringLiteral("创建临时文件失败: %1").arg(file.errorString());
        return result;
    }

    std::unique_ptr<QTextEncoder> encoder(codec->makeEncoder());
    if (!encoder) {
        result.ok = false;
        result.message = QStringLiteral("编码器初始化失败，无法保存文件。");
        file.cancelWriting();
        return result;
    }

    QString writeError;
    const bool traverseOk = buffer.forEachSegment([&](const QString &source, int start, int length) {
        return encodeAndWriteRange(&file, encoder.get(), source, start, length, &writeError);
    });
    if (!traverseOk) {
        result.ok = false;
        result.message = writeError.isEmpty()
                             ? QStringLiteral("保存失败：写盘中断。")
                             : writeError;
        file.cancelWriting();
        return result;
    }

    try {
        const QByteArray tail = encoder->fromUnicode(QString());
        if (!tail.isEmpty() && file.write(tail) != tail.size()) {
            result.ok = false;
            result.message = QStringLiteral("写入文件失败: %1").arg(file.errorString());
            file.cancelWriting();
            return result;
        }
    } catch (const std::bad_alloc &) {
        result.ok = false;
        result.message = QStringLiteral("保存失败：内存不足（编码阶段）。");
        file.cancelWriting();
        return result;
    } catch (const std::exception &ex) {
        result.ok = false;
        result.message = QStringLiteral("保存失败：%1").arg(QString::fromUtf8(ex.what()));
        file.cancelWriting();
        return result;
    } catch (...) {
        result.ok = false;
        result.message = QStringLiteral("保存失败：未知异常。");
        file.cancelWriting();
        return result;
    }

    if (!file.commit()) {
        result.ok = false;
        result.message = QStringLiteral("提交保存失败: %1").arg(file.errorString());
        return result;
    }

    result.ok = true;
    result.message = QStringLiteral("保存成功");
    return result;
}

DocumentSession::SaveResult DocumentSession::writeFile(
    const QString &targetPath,
    const PieceTableBuffer::SaveSnapshot &snapshot,
    const QString &codecName)
{
    SaveResult result;
    result.targetPath = targetPath;

    QTextCodec *codec = QTextCodec::codecForName(codecName.toUtf8());
    if (!codec) {
        codec = QTextCodec::codecForName("UTF-8");
    }

    if (!codec) {
        result.ok = false;
        result.message = QStringLiteral("编码器不可用，无法保存文件。");
        return result;
    }

    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        result.ok = false;
        result.message = QStringLiteral("创建临时文件失败: %1").arg(file.errorString());
        return result;
    }

    std::unique_ptr<QTextEncoder> encoder(codec->makeEncoder());
    if (!encoder) {
        result.ok = false;
        result.message = QStringLiteral("编码器初始化失败，无法保存文件。");
        file.cancelWriting();
        return result;
    }

    try {
        for (const PieceTableBuffer::SnapshotSegment &segment : snapshot.segments) {
            if (segment.length <= 0) {
                continue;
            }
            const QString &source = segment.fromAddBuffer ? snapshot.add : snapshot.original;
            QString writeError;
            if (!encodeAndWriteRange(&file,
                                     encoder.get(),
                                     source,
                                     segment.start,
                                     segment.length,
                                     &writeError)) {
                result.ok = false;
                result.message = writeError.isEmpty()
                                     ? QStringLiteral("保存失败：写盘中断。")
                                     : writeError;
                file.cancelWriting();
                return result;
            }
        }

        const QByteArray tail = encoder->fromUnicode(QString());
        if (!tail.isEmpty() && file.write(tail) != tail.size()) {
            result.ok = false;
            result.message = QStringLiteral("写入文件失败: %1").arg(file.errorString());
            file.cancelWriting();
            return result;
        }
    } catch (const std::bad_alloc &) {
        result.ok = false;
        result.message = QStringLiteral("保存失败：内存不足（编码阶段）。");
        file.cancelWriting();
        return result;
    } catch (const std::exception &ex) {
        result.ok = false;
        result.message = QStringLiteral("保存失败：%1").arg(QString::fromUtf8(ex.what()));
        file.cancelWriting();
        return result;
    } catch (...) {
        result.ok = false;
        result.message = QStringLiteral("保存失败：未知异常。");
        file.cancelWriting();
        return result;
    }

    if (!file.commit()) {
        result.ok = false;
        result.message = QStringLiteral("提交保存失败: %1").arg(file.errorString());
        return result;
    }

    result.ok = true;
    result.message = QStringLiteral("保存成功");
    return result;
}
