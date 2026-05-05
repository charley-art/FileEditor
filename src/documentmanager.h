#ifndef DOCUMENTMANAGER_H
#define DOCUMENTMANAGER_H

#include <QHash>
#include <QObject>
#include <QSharedPointer>

class DocumentSession;

class DocumentManager : public QObject
{
    Q_OBJECT
public:
    explicit DocumentManager(QObject *parent = nullptr);

    QSharedPointer<DocumentSession> createUntitled();
    QSharedPointer<DocumentSession> openOrGet(const QString &path, bool *alreadyOpen, QString *errorMessage);

    bool isOpen(const QString &path) const;
    void unregisterSession(const QSharedPointer<DocumentSession> &session);

private:
    QString normalize(const QString &path) const;
    void cleanupDeadEntries() const;
    void bindPathTracking(const QSharedPointer<DocumentSession> &session);

    mutable QHash<QString, QWeakPointer<DocumentSession>> m_openByNormalizedPath;
};

#endif // DOCUMENTMANAGER_H
