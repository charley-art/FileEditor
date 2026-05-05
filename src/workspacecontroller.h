#ifndef WORKSPACECONTROLLER_H
#define WORKSPACECONTROLLER_H

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QSharedPointer>
#include <QVector>

#include "documentmanager.h"
#include "documentsession.h"

class WorkspaceController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int paneCount READ paneCount NOTIFY paneCountChanged)
    Q_PROPERTY(int focusedPaneIndex READ focusedPaneIndex NOTIFY focusedPaneIndexChanged)
    Q_PROPERTY(bool canOpenMore READ canOpenMore NOTIFY paneCountChanged)
    Q_PROPERTY(bool anySaving READ anySaving NOTIFY anySavingChanged)
    Q_PROPERTY(bool anyOpening READ anyOpening NOTIFY anyOpeningChanged)
    Q_PROPERTY(int pasteLimitBytes READ pasteLimitBytes WRITE setPasteLimitBytes NOTIFY pasteLimitBytesChanged)

public:
    enum PaneRoles {
        OccupiedRole = Qt::UserRole + 1,
        TitleRole,
        FilePathRole,
        DirtyRole,
        SavingRole,
        FocusedRole,
        CurrentLineRole,
        TotalLinesRole,
        PercentRole,
        CursorPositionRole,
        MultiSelectRole,
        DocumentSessionRole,
        CanEditRole,
        TextLengthRole,
        LargeFileRole,
        TextRevisionRole,
        SearchQueryRole,
        MatchCountRole,
        MatchCountDisplayRole,
        CurrentMatchRole,
        ReplaceAllEnabledRole,
        SearchingRole
    };

    explicit WorkspaceController(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int paneCount() const;
    int focusedPaneIndex() const;
    bool canOpenMore() const;
    bool anySaving() const;
    bool anyOpening() const;
    int pasteLimitBytes() const;
    void setPasteLimitBytes(int bytes);

    Q_INVOKABLE void newFile();
    Q_INVOKABLE void openFile();
    Q_INVOKABLE void openMore();
    Q_INVOKABLE void closeFocused();
    Q_INVOKABLE void saveFocused();
    Q_INVOKABLE void saveFocusedAs();

    Q_INVOKABLE void setFocusedPane(int slot);
    Q_INVOKABLE void updateCursorPosition(int slot, int position);
    Q_INVOKABLE void setMultiSelectEnabled(int slot, bool enabled);

    Q_INVOKABLE bool canPaste(const QString &text) const;
    Q_INVOKABLE QString clipboardText() const;
    Q_INVOKABLE void setClipboardText(const QString &text);
    Q_INVOKABLE void setSearchQuery(int slot, const QString &query);
    Q_INVOKABLE QString searchQueryAt(int slot) const;
    Q_INVOKABLE int findNext(int slot);
    Q_INVOKABLE int findPrevious(int slot);
    Q_INVOKABLE int currentMatchPosition(int slot) const;
    Q_INVOKABLE int queryLength(int slot) const;
    Q_INVOKABLE bool replaceCurrent(int slot, const QString &replacement);
    Q_INVOKABLE int replaceAll(int slot, const QString &replacement);
    Q_INVOKABLE QString matchStatus(int slot) const;
    Q_INVOKABLE bool replaceAllEnabledAt(int slot) const;
    Q_INVOKABLE bool isSearchingAt(int slot) const;
    Q_INVOKABLE bool isSlotOccupied(int slot) const;
    Q_INVOKABLE bool prepareForAppClose();
    Q_INVOKABLE QString lineText(int slot, int zeroBasedLine) const;
    int lineLength(int slot, int zeroBasedLine) const;
    QString lineTextSlice(int slot, int zeroBasedLine, int startColumn, int maxChars) const;
    Q_INVOKABLE int lineStartOffset(int slot, int zeroBasedLine) const;
    Q_INVOKABLE QString textSlice(int slot, int start, int length) const;
    Q_INVOKABLE int textLength(int slot) const;
    Q_INVOKABLE int lineForOffset(int slot, int offset) const;
    Q_INVOKABLE int applyTextEdit(int slot, int position, int removeLength, const QString &insertedText);
    Q_INVOKABLE bool undoEdit(int slot);
    Q_INVOKABLE bool redoEdit(int slot);
    Q_INVOKABLE bool replaceLineText(int slot, int zeroBasedLine, const QString &lineText);
    Q_INVOKABLE bool deleteLineAt(int slot, int zeroBasedLine);
    QVector<int> searchMatchPositionsInRange(int slot, int start, int endExclusive, int maxCount) const;

signals:
    void paneCountChanged();
    void focusedPaneIndexChanged();
    void anySavingChanged();
    void anyOpeningChanged();
    void pasteLimitBytesChanged();
    void toastRequested(const QString &message);

private:
    enum class OpenMode {
        ReplaceFocused,
        AddMore
    };

    struct PendingOpenRequest {
        OpenMode mode = OpenMode::ReplaceFocused;
        int targetSlot = -1;
        QString path;
    };

    struct PaneSlot {
        bool occupied = false;
        QSharedPointer<DocumentSession> document;
        bool multiSelectEnabled = false;
        int textRevision = 0;
    };

    static constexpr int kMaxPaneCount = 4;

    int firstEmptySlot() const;
    int occupiedCount() const;
    int findSlotByPath(const QString &path) const;
    int findSlotByDocument(DocumentSession *doc) const;
    bool hasAnySavingSession() const;
    void notifyAllCanEditChanged();
    void refreshAnySavingState();

    bool ensureCanDiscardSlot(int slot);
    bool ensureSlotEditableForContentChange(int slot);

    void startAsyncOpen(const QString &path, OpenMode mode, int targetSlot);
    void handleAsyncOpenFinished(const DocumentSession::DecodedFileResult &result);

    void assignSessionToSlot(int slot, const QSharedPointer<DocumentSession> &session);
    void clearSlot(int slot);
    void closeSlot(int slot);
    void notifySlotChanged(int slot, const QVector<int> &roles = {});
    void connectDocumentSignals(const QSharedPointer<DocumentSession> &session);

    void focusSlot(int slot);
    void showWarning(const QString &message) const;

    QVector<PaneSlot> m_slots;
    int m_focusedPaneIndex = -1;
    DocumentManager m_documentManager;
    QPointer<QFutureWatcher<DocumentSession::DecodedFileResult>> m_openWatcher;
    PendingOpenRequest m_pendingOpen;
    int m_pasteLimitBytes = 10 * 1024;
    bool m_anySaving = false;
};

#endif // WORKSPACECONTROLLER_H
