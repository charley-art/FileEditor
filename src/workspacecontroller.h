#ifndef WORKSPACECONTROLLER_H
#define WORKSPACECONTROLLER_H

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QSharedPointer>
#include <QVector>

#include "documentsession.h"

class WorkspaceController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY sessionCountChanged)
    Q_PROPERTY(qint64 focusedSessionId READ focusedSessionId NOTIFY focusedSessionIdChanged)
    Q_PROPERTY(bool canOpenMore READ canOpenMore NOTIFY sessionCountChanged)
    Q_PROPERTY(bool anySaving READ anySaving NOTIFY anySavingChanged)
    Q_PROPERTY(bool anyOpening READ anyOpening NOTIFY anyOpeningChanged)
    Q_PROPERTY(int pasteLimitBytes READ pasteLimitBytes WRITE setPasteLimitBytes NOTIFY pasteLimitBytesChanged)

public:
    enum SessionRoles {
        DocumentSessionRole = Qt::UserRole + 1
    };

    explicit WorkspaceController(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int sessionCount() const;
    qint64 focusedSessionId() const;
    bool canOpenMore() const;
    bool anySaving() const;
    bool anyOpening() const;
    int pasteLimitBytes() const;
    void setPasteLimitBytes(int bytes);

    Q_INVOKABLE void newFile();
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openMore(const QString &path);
    Q_INVOKABLE void closeFocused();
    Q_INVOKABLE void saveFocused();
    Q_INVOKABLE void saveFocusedAs();

    Q_INVOKABLE void focusSession(qint64 sessionId);

    Q_INVOKABLE bool prepareForAppClose();

signals:
    void sessionCountChanged();
    void focusedSessionIdChanged();
    void anySavingChanged();
    void anyOpeningChanged();
    void pasteLimitBytesChanged();
    void toastRequested(const QString &message);

private:
    enum class OpenMode {
        LoadFocused,
        AddNew
    };

    struct PendingOpenRequest {
        OpenMode mode = OpenMode::LoadFocused;
        qint64 targetSessionId = 0;
        QString path;
    };

    qint64 allocateSessionId();
    QSharedPointer<DocumentSession> createSession();
    QSharedPointer<DocumentSession> focusedSession() const;
    QSharedPointer<DocumentSession> sessionById(qint64 sessionId) const;
    int indexForSessionId(qint64 sessionId) const;
    int findSessionIndexByPath(const QString &path) const;
    bool hasAnySavingSession() const;
    void refreshAnySavingState();

    bool ensureCanDiscardSession(qint64 sessionId);
    bool ensureSessionEditableForContentChange(qint64 sessionId);

    void startAsyncOpen(const QString &path, OpenMode mode, qint64 targetSessionId);
    void handleAsyncOpenFinished(const DocumentSession::DecodedFileResult &result);

    void appendSession(const QSharedPointer<DocumentSession> &session);
    void closeSessionAt(int row);
    void notifySessionChanged(int row, const QVector<int> &roles = {});
    void connectDocumentSignals(const QSharedPointer<DocumentSession> &session);

    void focusSessionAt(int row);
    void showWarning(const QString &message) const;

    QVector<QSharedPointer<DocumentSession>> m_sessions;
    qint64 m_focusedSessionId = 0;
    qint64 m_nextSessionId = 1;
    QPointer<QFutureWatcher<DocumentSession::DecodedFileResult>> m_openWatcher;
    PendingOpenRequest m_pendingOpen;
    int m_pasteLimitBytes = 0;
    bool m_anySaving = false;
};

#endif // WORKSPACECONTROLLER_H
