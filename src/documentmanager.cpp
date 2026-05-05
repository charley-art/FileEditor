#include "documentmanager.h"

#include "documentsession.h"
#include "pathutils.h"

#include <QMutableHashIterator>

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
{
}

QSharedPointer<DocumentSession> DocumentManager::createUntitled()
{
    QSharedPointer<DocumentSession> session(new DocumentSession);
    bindPathTracking(session);
    return session;
}

QSharedPointer<DocumentSession> DocumentManager::openOrGet(const QString &path, bool *alreadyOpen, QString *errorMessage)
{
    cleanupDeadEntries();

    const QString normalized = normalize(path);
    if (alreadyOpen) {
        *alreadyOpen = false;
    }

    if (!normalized.isEmpty() && m_openByNormalizedPath.contains(normalized)) {
        const QSharedPointer<DocumentSession> existing = m_openByNormalizedPath.value(normalized).toStrongRef();
        if (existing) {
            if (alreadyOpen) {
                *alreadyOpen = true;
            }
            return existing;
        }
    }

    QSharedPointer<DocumentSession> session(new DocumentSession);
    QString openError;
    if (!session->loadFromFile(path, &openError)) {
        if (errorMessage) {
            *errorMessage = openError;
        }
        return {};
    }

    bindPathTracking(session);
    if (!normalized.isEmpty()) {
        m_openByNormalizedPath.insert(normalized, session.toWeakRef());
    }

    return session;
}

bool DocumentManager::isOpen(const QString &path) const
{
    cleanupDeadEntries();

    const QString normalized = normalize(path);
    if (normalized.isEmpty()) {
        return false;
    }

    return m_openByNormalizedPath.contains(normalized)
        && !m_openByNormalizedPath.value(normalized).isNull();
}

void DocumentManager::unregisterSession(const QSharedPointer<DocumentSession> &session)
{
    if (!session) {
        return;
    }

    QObject::disconnect(session.data(), nullptr, this, nullptr);

    QMutableHashIterator<QString, QWeakPointer<DocumentSession>> it(m_openByNormalizedPath);
    while (it.hasNext()) {
        it.next();
        const QSharedPointer<DocumentSession> candidate = it.value().toStrongRef();
        if (!candidate || candidate.data() == session.data()) {
            it.remove();
        }
    }
}

QString DocumentManager::normalize(const QString &path) const
{
    return PathUtils::normalizePath(path);
}

void DocumentManager::cleanupDeadEntries() const
{
    QMutableHashIterator<QString, QWeakPointer<DocumentSession>> it(m_openByNormalizedPath);
    while (it.hasNext()) {
        it.next();
        if (it.value().isNull()) {
            it.remove();
        }
    }
}

void DocumentManager::bindPathTracking(const QSharedPointer<DocumentSession> &session)
{
    const QWeakPointer<DocumentSession> sessionWeak = session.toWeakRef();
    QObject::connect(session.data(), &DocumentSession::filePathChanged, this, [this, sessionWeak]() {
        const QSharedPointer<DocumentSession> session = sessionWeak.toStrongRef();
        if (!session) {
            cleanupDeadEntries();
            return;
        }

        cleanupDeadEntries();

        QMutableHashIterator<QString, QWeakPointer<DocumentSession>> it(m_openByNormalizedPath);
        while (it.hasNext()) {
            it.next();
            const QSharedPointer<DocumentSession> candidate = it.value().toStrongRef();
            if (!candidate || candidate.data() == session.data()) {
                it.remove();
            }
        }

        const QString currentPath = session->filePath();
        const QString normalized = normalize(currentPath);
        if (!normalized.isEmpty()) {
            m_openByNormalizedPath.insert(normalized, session.toWeakRef());
        }
    });
}
