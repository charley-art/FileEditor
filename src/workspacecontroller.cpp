#include "workspacecontroller.h"

#include "editorconfig.h"
#include "pathutils.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QtConcurrent>

WorkspaceController::WorkspaceController(QObject *parent)
    : QAbstractListModel(parent)
    , m_pasteLimitBytes(EditorConfig::instance().defaultPasteLimitBytes())
{
}

int WorkspaceController::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_sessions.size();
}

QVariant WorkspaceController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sessions.size()) {
        return {};
    }

    const QSharedPointer<DocumentSession> &doc = m_sessions.at(index.row());
    switch (role) {
    case DocumentSessionRole:
        return QVariant::fromValue(static_cast<QObject *>(doc.data()));
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> WorkspaceController::roleNames() const
{
    return {
        { DocumentSessionRole, "documentSession" }
    };
}

int WorkspaceController::sessionCount() const
{
    return m_sessions.size();
}

qint64 WorkspaceController::focusedSessionId() const
{
    return m_focusedSessionId;
}

bool WorkspaceController::canOpenMore() const
{
    return m_sessions.size() < EditorConfig::instance().maxPaneCount();
}

bool WorkspaceController::anySaving() const
{
    return m_anySaving;
}

bool WorkspaceController::anyOpening() const
{
    return m_openWatcher != nullptr;
}

int WorkspaceController::pasteLimitBytes() const
{
    return m_pasteLimitBytes;
}

void WorkspaceController::setPasteLimitBytes(int bytes)
{
    const EditorConfig &config = EditorConfig::instance();
    const int clamped = qBound(config.minPasteLimitBytes(), bytes, config.maxPasteLimitBytes());
    if (m_pasteLimitBytes == clamped) {
        return;
    }

    m_pasteLimitBytes = clamped;
    emit pasteLimitBytesChanged();
}

void WorkspaceController::newFile()
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Editing is disabled for now."));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    if (m_sessions.isEmpty()) {
        const QSharedPointer<DocumentSession> session = createSession();
        appendSession(session);
        focusSession(session->sessionId());
        return;
    }

    if (!focusedSession()) {
        focusSessionAt(0);
    }

    const qint64 targetSessionId = m_focusedSessionId;
    if (!ensureCanDiscardSession(targetSessionId)) {
        return;
    }

    if (!ensureSessionEditableForContentChange(targetSessionId)) {
        return;
    }

    const QSharedPointer<DocumentSession> session = sessionById(targetSessionId);
    if (!session) {
        return;
    }

    session->applyLoadedFile(QString(), QString(), QStringLiteral("UTF-8"));
    notifySessionChanged(indexForSessionId(targetSessionId));
}

void WorkspaceController::openFile(const QString &path)
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Cannot open another file right now."));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    if (path.isEmpty()) {
        return;
    }

    const int existingSessionIndex = findSessionIndexByPath(path);
    if (existingSessionIndex >= 0) {
        focusSessionAt(existingSessionIndex);
        emit toastRequested(QStringLiteral("This file is already open. Focused the existing document."));
        return;
    }

    if (m_sessions.isEmpty()) {
        startAsyncOpen(path, OpenMode::AddNew, 0);
        return;
    }

    if (!focusedSession()) {
        focusSessionAt(0);
    }

    const qint64 targetSessionId = m_focusedSessionId;
    if (!ensureCanDiscardSession(targetSessionId)) {
        return;
    }

    if (!ensureSessionEditableForContentChange(targetSessionId)) {
        return;
    }

    startAsyncOpen(path, OpenMode::LoadFocused, targetSessionId);
}

void WorkspaceController::openMore(const QString &path)
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Cannot open another file right now."));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    if (!canOpenMore()) {
        emit toastRequested(QStringLiteral("Up to 4 documents are supported."));
        return;
    }

    if (path.isEmpty()) {
        return;
    }

    const int existingSessionIndex = findSessionIndexByPath(path);
    if (existingSessionIndex >= 0) {
        focusSessionAt(existingSessionIndex);
        emit toastRequested(QStringLiteral("This file is already open. Focused the existing document."));
        return;
    }

    startAsyncOpen(path, OpenMode::AddNew, 0);
}

void WorkspaceController::closeFocused()
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Cannot close a document right now."));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    const int sessionIndex = indexForSessionId(m_focusedSessionId);
    const QSharedPointer<DocumentSession> session = focusedSession();
    if (sessionIndex < 0 || !session) {
        return;
    }

    if (!ensureCanDiscardSession(session->sessionId())) {
        return;
    }

    if (session->isSaving()) {
        emit toastRequested(QStringLiteral("Saving is in progress. Cannot close this document."));
        return;
    }

    closeSessionAt(sessionIndex);
}

void WorkspaceController::saveFocused()
{
    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    const QSharedPointer<DocumentSession> session = focusedSession();
    if (!session) {
        return;
    }

    if (session->isSaving()) {
        emit toastRequested(QStringLiteral("Saving is in progress. Please wait."));
        return;
    }
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Cannot start another save right now."));
        return;
    }

    if (session->filePath().isEmpty()) {
        saveFocusedAs();
        return;
    }

    session->saveAsync();
}

void WorkspaceController::saveFocusedAs()
{
    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    const QSharedPointer<DocumentSession> session = focusedSession();
    if (!session) {
        return;
    }

    if (session->isSaving()) {
        emit toastRequested(QStringLiteral("Saving is in progress. Please wait."));
        return;
    }
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Cannot start another save right now."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Save As"), session->filePath());
    if (path.isEmpty()) {
        return;
    }

    const int existingSessionIndex = findSessionIndexByPath(path);
    if (existingSessionIndex >= 0 && existingSessionIndex != indexForSessionId(m_focusedSessionId)) {
        focusSessionAt(existingSessionIndex);
        emit toastRequested(QStringLiteral("The target file is open in another document. Focused it."));
        return;
    }

    session->saveAsAsync(path);
}

void WorkspaceController::focusSession(qint64 sessionId)
{
    if (indexForSessionId(sessionId) < 0) {
        return;
    }

    const qint64 previousSessionId = m_focusedSessionId;
    if (previousSessionId == sessionId) {
        return;
    }

    m_focusedSessionId = sessionId;

    emit focusedSessionIdChanged();
}

bool WorkspaceController::prepareForAppClose()
{
    if (m_openWatcher) {
        showWarning(QStringLiteral("A file is opening. Please wait before exiting."));
        return false;
    }

    if (hasAnySavingSession()) {
        showWarning(QStringLiteral("A file is being saved. Please wait before exiting."));
        return false;
    }

    for (int i = m_sessions.size() - 1; i >= 0; --i) {
        const QSharedPointer<DocumentSession> session = m_sessions.at(i);
        if (session && !ensureCanDiscardSession(session->sessionId())) {
            return false;
        }
    }

    return true;
}

qint64 WorkspaceController::allocateSessionId()
{
    return m_nextSessionId++;
}

QSharedPointer<DocumentSession> WorkspaceController::createSession()
{
    QSharedPointer<DocumentSession> session(new DocumentSession(allocateSessionId()));
    connectDocumentSignals(session);
    return session;
}

QSharedPointer<DocumentSession> WorkspaceController::focusedSession() const
{
    return sessionById(m_focusedSessionId);
}

QSharedPointer<DocumentSession> WorkspaceController::sessionById(qint64 sessionId) const
{
    if (sessionId <= 0) {
        return {};
    }

    for (const QSharedPointer<DocumentSession> &session : m_sessions) {
        if (session && session->sessionId() == sessionId) {
            return session;
        }
    }

    return {};
}

int WorkspaceController::indexForSessionId(qint64 sessionId) const
{
    if (sessionId <= 0) {
        return -1;
    }

    for (int i = 0; i < m_sessions.size(); ++i) {
        const QSharedPointer<DocumentSession> &session = m_sessions.at(i);
        if (session && session->sessionId() == sessionId) {
            return i;
        }
    }

    return -1;
}

int WorkspaceController::findSessionIndexByPath(const QString &path) const
{
    const QString normalized = PathUtils::normalizePath(path);
    if (normalized.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_sessions.size(); ++i) {
        const QSharedPointer<DocumentSession> &session = m_sessions.at(i);
        if (!session) {
            continue;
        }

        if (PathUtils::normalizePath(session->filePath()) == normalized) {
            return i;
        }
    }

    return -1;
}

bool WorkspaceController::hasAnySavingSession() const
{
    for (const QSharedPointer<DocumentSession> &session : m_sessions) {
        if (session && session->isSaving()) {
            return true;
        }
    }
    return false;
}

void WorkspaceController::refreshAnySavingState()
{
    const bool current = hasAnySavingSession();
    if (m_anySaving == current) {
        return;
    }

    m_anySaving = current;
    emit anySavingChanged();
}

bool WorkspaceController::ensureCanDiscardSession(qint64 sessionId)
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("A file is being saved. Editing is disabled for now."));
        return false;
    }

    const QSharedPointer<DocumentSession> session = sessionById(sessionId);
    if (!session) {
        return true;
    }

    if (session->isSaving()) {
        emit toastRequested(QStringLiteral("Saving is in progress. Editing is disabled."));
        return false;
    }

    if (!session->isDirty()) {
        return true;
    }

    QMessageBox messageBox;
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(QStringLiteral("Unsaved Changes"));
    messageBox.setText(QStringLiteral("This document has unsaved changes. Save them first?"));
    const QPushButton *saveButton = messageBox.addButton(QStringLiteral("Save"), QMessageBox::AcceptRole);
    const QPushButton *discardButton = messageBox.addButton(QStringLiteral("Do Not Save"), QMessageBox::DestructiveRole);
    messageBox.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    messageBox.exec();

    if (messageBox.clickedButton() == saveButton) {
        QString error;
        if (session->filePath().isEmpty()) {
            const QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Save File"));
            if (path.isEmpty()) {
                return false;
            }
            const int existingSessionIndex = findSessionIndexByPath(path);
            const int currentSessionIndex = indexForSessionId(sessionId);
            if (existingSessionIndex >= 0 && existingSessionIndex != currentSessionIndex) {
                focusSessionAt(existingSessionIndex);
                emit toastRequested(QStringLiteral("The target file is open in another document. Focused it."));
                return false;
            }
            if (!session->saveAsSync(path, &error)) {
                showWarning(error);
                return false;
            }
            notifySessionChanged(currentSessionIndex);
            return true;
        }

        if (!session->saveSync(&error)) {
            showWarning(error);
            return false;
        }

        notifySessionChanged(indexForSessionId(sessionId));
        return true;
    }

    if (messageBox.clickedButton() == discardButton) {
        return true;
    }

    return false;
}

bool WorkspaceController::ensureSessionEditableForContentChange(qint64 sessionId)
{
    const QSharedPointer<DocumentSession> session = sessionById(sessionId);
    if (!session) {
        return true;
    }

    if (m_openWatcher
        && m_pendingOpen.mode == OpenMode::LoadFocused
        && m_pendingOpen.targetSessionId == sessionId) {
        emit toastRequested(QStringLiteral("Opening is in progress. Editing is disabled for this document."));
        return false;
    }

    if (session->isSaving()) {
        emit toastRequested(QStringLiteral("Saving is in progress. Editing is disabled."));
        return false;
    }

    return true;
}

void WorkspaceController::startAsyncOpen(const QString &path, OpenMode mode, qint64 targetSessionId)
{
    if (path.isEmpty()) {
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("Opening a file. Please wait."));
        return;
    }

    m_pendingOpen.mode = mode;
    m_pendingOpen.targetSessionId = targetSessionId;
    m_pendingOpen.path = path;

    if (mode == OpenMode::LoadFocused) {
        const QSharedPointer<DocumentSession> session = sessionById(targetSessionId);
        if (session) {
            session->setExternalEditBlocked(true);
        }
    }

    auto *watcher = new QFutureWatcher<DocumentSession::DecodedFileResult>(this);
    m_openWatcher = watcher;
    emit anyOpeningChanged();

    connect(watcher, &QFutureWatcher<DocumentSession::DecodedFileResult>::finished, this, [this, watcher]() {
        const DocumentSession::DecodedFileResult result = watcher->result();
        watcher->deleteLater();

        if (m_openWatcher == watcher) {
            m_openWatcher = nullptr;
            emit anyOpeningChanged();
        }

        handleAsyncOpenFinished(result);
    });

    watcher->setFuture(QtConcurrent::run([path]() {
        return DocumentSession::decodeFileForLoad(path);
    }));

    emit toastRequested(QStringLiteral("Opening a file. Please wait..."));
}

void WorkspaceController::handleAsyncOpenFinished(const DocumentSession::DecodedFileResult &result)
{
    const PendingOpenRequest request = m_pendingOpen;
    m_pendingOpen = PendingOpenRequest();

    if (request.mode == OpenMode::LoadFocused) {
        const QSharedPointer<DocumentSession> session = sessionById(request.targetSessionId);
        if (session) {
            session->setExternalEditBlocked(false);
        }
    }

    if (!result.ok) {
        showWarning(result.error);
        return;
    }

    const QString openedPath = result.path.isEmpty() ? request.path : result.path;
    const int existingSessionIndex = findSessionIndexByPath(openedPath);
    if (existingSessionIndex >= 0) {
        focusSessionAt(existingSessionIndex);
        emit toastRequested(QStringLiteral("This file is already open. Focused the existing document."));
        return;
    }

    if (request.mode == OpenMode::AddNew || request.targetSessionId <= 0) {
        if (!canOpenMore()) {
            emit toastRequested(QStringLiteral("Up to 4 documents are supported."));
            return;
        }

        const QSharedPointer<DocumentSession> session = createSession();
        session->applyLoadedFile(openedPath, result.text, result.codec);
        appendSession(session);
        focusSession(session->sessionId());
        return;
    }

    const QSharedPointer<DocumentSession> session = sessionById(request.targetSessionId);
    if (!session) {
        emit toastRequested(QStringLiteral("The target document is no longer available."));
        return;
    }

    session->applyLoadedFile(openedPath, result.text, result.codec);
    notifySessionChanged(indexForSessionId(session->sessionId()));
    focusSession(session->sessionId());
}

void WorkspaceController::appendSession(const QSharedPointer<DocumentSession> &session)
{
    if (!session || m_sessions.size() >= EditorConfig::instance().maxPaneCount()) {
        return;
    }

    const int row = m_sessions.size();
    beginInsertRows(QModelIndex(), row, row);
    m_sessions.push_back(session);
    endInsertRows();

    emit sessionCountChanged();
    refreshAnySavingState();
}

void WorkspaceController::closeSessionAt(int row)
{
    if (row < 0 || row >= m_sessions.size()) {
        return;
    }

    const qint64 previousFocusedSessionId = m_focusedSessionId;
    const qint64 removedSessionId = m_sessions.at(row) ? m_sessions.at(row)->sessionId() : 0;

    beginRemoveRows(QModelIndex(), row, row);
    if (m_sessions.at(row)) {
        QObject::disconnect(m_sessions.at(row).data(), nullptr, this, nullptr);
    }
    m_sessions.removeAt(row);

    if (m_sessions.isEmpty()) {
        m_focusedSessionId = 0;
    } else if (previousFocusedSessionId == removedSessionId) {
        const int nextRow = qMin(row, m_sessions.size() - 1);
        m_focusedSessionId = m_sessions.at(nextRow)->sessionId();
    }
    endRemoveRows();

    emit sessionCountChanged();
    if (previousFocusedSessionId != m_focusedSessionId) {
        emit focusedSessionIdChanged();
    }
    refreshAnySavingState();
}

void WorkspaceController::notifySessionChanged(int row, const QVector<int> &roles)
{
    if (row < 0 || row >= m_sessions.size()) {
        return;
    }

    emit dataChanged(index(row, 0), index(row, 0), roles);
}

void WorkspaceController::connectDocumentSignals(const QSharedPointer<DocumentSession> &session)
{
    if (!session) {
        return;
    }

    DocumentSession *doc = session.data();

    QObject::connect(doc, &DocumentSession::savingChanged, this, [this]() {
        refreshAnySavingState();
    });

    QObject::connect(doc, &DocumentSession::operationBlocked, this, [this](const QString &message) {
        emit toastRequested(message);
    });

    QObject::connect(doc, &DocumentSession::saveFinished, this, [this](bool ok, const QString &message) {
        if (!message.isEmpty()) {
            emit toastRequested(message);
        }
        if (!ok) {
            showWarning(message);
        }
    });
}

void WorkspaceController::focusSessionAt(int row)
{
    if (row < 0 || row >= m_sessions.size() || !m_sessions.at(row)) {
        return;
    }

    focusSession(m_sessions.at(row)->sessionId());
}

void WorkspaceController::showWarning(const QString &message) const
{
    if (message.isEmpty()) {
        return;
    }

    QMessageBox::warning(nullptr, QStringLiteral("Notice"), message);
}
