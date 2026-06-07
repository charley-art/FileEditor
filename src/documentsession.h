#ifndef DOCUMENTSESSION_H
#define DOCUMENTSESSION_H

#include <atomic>
#include <memory>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>

#include "lineindex.h"
#include "piecetablebuffer.h"

class DocumentSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 sessionId READ sessionId CONSTANT)
    Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
    Q_PROPERTY(QString displayPath READ displayPath NOTIFY displayPathChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(bool dirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool saving READ isSaving NOTIFY savingChanged)
    Q_PROPERTY(QString codecName READ codecName NOTIFY codecChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY lineCountChanged)
    Q_PROPERTY(int currentLine READ currentLine NOTIFY currentLineChanged)
    Q_PROPERTY(int currentLinePercent READ currentLinePercent NOTIFY currentLineChanged)
    Q_PROPERTY(int textLength READ textLength NOTIFY textMetricsChanged)
    Q_PROPERTY(int textRevision READ textRevision NOTIFY textRevisionChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchStateChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY searchStateChanged)
    Q_PROPERTY(QString matchCountDisplay READ matchCountDisplay NOTIFY searchStateChanged)
    Q_PROPERTY(int currentMatch READ currentMatch NOTIFY searchStateChanged)
    Q_PROPERTY(bool replaceAllEnabled READ replaceAllEnabled NOTIFY searchStateChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchStateChanged)
    Q_PROPERTY(bool canModify READ canModify NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool multiSelectEnabled READ multiSelectEnabled WRITE setMultiSelectEnabled NOTIFY multiSelectEnabledChanged)

public:
    struct DecodedFileResult {
        bool ok = false;
        QString path;
        QString text;
        QString codec;
        QString error;
    };

    explicit DocumentSession(qint64 sessionId = 0, QObject *parent = nullptr);

    qint64 sessionId() const;
    QString filePath() const;
    QString displayPath() const;
    QString text() const;
    bool isDirty() const;
    bool isSaving() const;
    QString codecName() const;
    int lineCount() const;
    int currentLine() const;
    int currentLinePercent() const;
    int textRevision() const;
    int cursorPosition() const;
    int textLength() const;
    int lineStartOffset(int zeroBasedLine) const;
    int lineLengthAt(int zeroBasedLine) const;
    QString lineTextAt(int zeroBasedLine) const;
    QString lineTextSliceAt(int zeroBasedLine, int startColumn, int maxChars) const;
    QString textSlice(int start, int length) const;
    int lineForOffsetZeroBased(int offset) const;
    bool replaceLineText(int zeroBasedLine, const QString &lineText);
    bool deleteLineAt(int zeroBasedLine);
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();
    QString searchQuery() const;
    int matchCount() const;
    QString matchCountDisplay() const;
    int currentMatch() const;
    bool replaceAllEnabled() const;
    bool searching() const;
    bool multiSelectEnabled() const;
    void setMultiSelectEnabled(bool enabled);

    bool loadFromFile(const QString &path, QString *errorMessage);
    static DecodedFileResult decodeFileForLoad(const QString &path);
    void applyLoadedFile(const QString &path, const QString &decodedText, const QString &codecName);
    bool saveSync(QString *errorMessage);
    bool saveAsSync(const QString &path, QString *errorMessage);
    void saveAsync();
    void saveAsAsync(const QString &path);

    bool canModify() const;
    void setExternalEditBlocked(bool blocked);
    bool setTextFromEditor(const QString &newText);
    int applyTextEdit(int position, int removeLength, const QString &insertedText);
    void setCursorPosition(int position);

    Q_INVOKABLE void setSearchQuery(const QString &query);
    Q_INVOKABLE int findNext();
    Q_INVOKABLE int findPrevious();
    Q_INVOKABLE int currentMatchPosition() const;
    Q_INVOKABLE int queryLength() const;
    QVector<int> searchMatchPositionsInRange(int start, int endExclusive, int maxCount) const;
    Q_INVOKABLE bool replaceCurrent(const QString &replacement);
    Q_INVOKABLE int replaceAll(const QString &replacement);
    Q_INVOKABLE void toggleMultiSelectEnabled();

signals:
    void filePathChanged();
    void displayPathChanged();
    void textChanged();
    void dirtyChanged();
    void savingChanged();
    void codecChanged();
    void lineCountChanged();
    void currentLineChanged();

    void textMetricsChanged();
    void textRevisionChanged();
    void searchStateChanged();
    void editCapabilitiesChanged();
    void multiSelectEnabledChanged();
    void operationBlocked(const QString &message);
    void saveFinished(bool ok, const QString &message);

private:
    struct SaveResult {
        bool ok = false;
        QString message;
        QString targetPath;
    };

    struct SearchComputeResult {
        quint64 requestId = 0;
        quint64 contentRevision = 0;
        QString query;
        QVector<int> positions;
        bool overflow = false;
        int total = 0;
    };

    struct EditCommand {
        int position = 0;
        QString removedText;
        QString insertedText;
        int cursorBefore = 0;
        int cursorAfter = 0;
        quint64 fromState = 0;
        quint64 toState = 0;
    };

    bool saveToPathSync(const QString &path, QString *errorMessage);
    void beginAsyncSave(const QString &targetPath);
    bool applyEditInternal(int position, int removeLength, const QString &insertedText, bool recordHistory = true);
    void recoverAfterEditFailure();
    void updateCurrentLineFromCursor();
    void rebuildSearchCache();
    void startQueuedSearch();
    void applySearchResult(const SearchComputeResult &result);
    void setDirtyInternal(bool dirty);
    void setFilePathInternal(const QString &path);
    int commandBytes(const EditCommand &cmd) const;
    void clearEditHistory();
    bool pushUndoCommand(const EditCommand &cmd);
    bool pushRedoCommand(const EditCommand &cmd);
    void trimHistoryByLimit(QVector<EditCommand> &history, int &bytes);
    static SearchComputeResult computeSearch(const QString &text,
                                             const QString &query,
                                             quint64 requestId,
                                             quint64 contentRevision,
                                             const std::atomic<quint64> *latestRequestId);
    static SearchComputeResult computeSearch(const PieceTableBuffer::SaveSnapshot &snapshot,
                                             const QString &query,
                                             quint64 requestId,
                                             quint64 contentRevision,
                                             const std::atomic<quint64> *latestRequestId);
    QString currentTextSnapshot() const;
    static bool decodeText(const QByteArray &bytes, QString *decoded, QString *codecUsed);
    static bool isValidUtf8(const QByteArray &bytes);
    static SaveResult writeFile(const QString &targetPath,
                                const PieceTableBuffer &buffer,
                                const QString &codecName);
    static SaveResult writeFile(const QString &targetPath,
                                const PieceTableBuffer::SaveSnapshot &snapshot,
                                const QString &codecName);

    qint64 m_sessionId = 0;
    QString m_filePath;
    PieceTableBuffer m_buffer;
    LineIndex m_lineIndex;
    QString m_cachedText;
    bool m_fullTextCacheEnabled = true;
    QString m_codecName;
    bool m_dirty = false;
    bool m_saving = false;
    bool m_externalEditBlocked = false;
    bool m_multiSelectEnabled = false;
    int m_currentLine = 1;
    int m_cursorPosition = 0;
    int m_textRevision = 0;

    QString m_searchQuery;
    QVector<int> m_matchPositions;
    bool m_matchOverflow = false;
    int m_totalMatchCount = 0;
    int m_currentMatchIndex = -1;
    quint64 m_contentRevision = 0;
    quint64 m_searchRequestId = 0;
    quint64 m_runningSearchRequestId = 0;
    quint64 m_queuedSearchRequestId = 0;
    quint64 m_queuedSearchRevision = 0;
    QString m_queuedSearchQuery;
    bool m_searchQueued = false;
    bool m_searchRunning = false;
    QTimer m_searchDebounceTimer;
    QPointer<QFutureWatcher<SearchComputeResult>> m_searchWatcher;
    std::shared_ptr<std::atomic<quint64>> m_latestSearchRequestId;

    QVector<EditCommand> m_undoHistory;
    QVector<EditCommand> m_redoHistory;
    int m_undoBytes = 0;
    int m_redoBytes = 0;
    quint64 m_stateId = 0;
    quint64 m_savedStateId = 0;
    quint64 m_nextStateId = 1;

    QPointer<QFutureWatcher<SaveResult>> m_saveWatcher;
};

#endif // DOCUMENTSESSION_H
