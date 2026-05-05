#include "workspacecontroller.h"

#include "documentsession.h"
#include "pathutils.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QtConcurrent>

WorkspaceController::WorkspaceController(QObject *parent)
    : QAbstractListModel(parent)
    , m_slots(kMaxPaneCount)
{
}

int WorkspaceController::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return occupiedCount();
}

QVariant WorkspaceController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= occupiedCount()) {
        return {};
    }

    const PaneSlot &slot = m_slots.at(index.row());
    if (role == OccupiedRole) {
        return true;
    }

    if (!slot.document) {
        switch (role) {
        case TitleRole: return QStringLiteral("空白窗口");
        case FilePathRole: return QString();
        case DirtyRole: return false;
        case SavingRole: return false;
        case FocusedRole: return false;
        case CurrentLineRole: return 0;
        case TotalLinesRole: return 0;
        case PercentRole: return 0;
        case CursorPositionRole: return 0;
        case MultiSelectRole: return false;
        case CanEditRole: return false;
        case CanUndoRole: return false;
        case CanRedoRole: return false;
        case TextLengthRole: return 0;
        case LargeFileRole: return false;
        case TextRevisionRole: return 0;
        case SearchQueryRole: return QString();
        case MatchCountRole: return 0;
        case MatchCountDisplayRole: return QStringLiteral("0");
        case CurrentMatchRole: return 0;
        case ReplaceAllEnabledRole: return false;
        case SearchingRole: return false;
        default:
            return {};
        }
    }

    const auto &doc = slot.document;

    switch (role) {
    case TitleRole:
        return doc->isDirty()
            ? QStringLiteral("%1 *").arg(doc->displayPath())
            : doc->displayPath();
    case FilePathRole:
        return doc->filePath();
    case DirtyRole:
        return doc->isDirty();
    case SavingRole:
        return doc->isSaving();
    case FocusedRole:
        return m_focusedPaneIndex == index.row();
    case CurrentLineRole:
        return doc->currentLine();
    case TotalLinesRole:
        return doc->lineCount();
    case PercentRole:
        return doc->currentLinePercent();
    case CursorPositionRole:
        return doc->cursorPosition();
    case MultiSelectRole:
        return slot.multiSelectEnabled;
    case CanEditRole:
        if (hasAnySavingSession()) {
            return false;
        }
        if (m_openWatcher
            && m_pendingOpen.mode == OpenMode::ReplaceFocused
            && m_pendingOpen.targetSlot == index.row()) {
            return false;
        }
        return doc->canModify();
    case CanUndoRole:
        return doc->canUndo();
    case CanRedoRole:
        return doc->canRedo();
    case TextLengthRole:
        return doc->textLength();
    case LargeFileRole:
        return doc->textLength() > (2 * 1024 * 1024);
    case TextRevisionRole:
        return slot.textRevision;
    case SearchQueryRole:
        return doc->searchQuery();
    case MatchCountRole:
        return doc->matchCount();
    case MatchCountDisplayRole:
        return doc->matchCountDisplay();
    case CurrentMatchRole:
        return doc->currentMatch();
    case ReplaceAllEnabledRole:
        return doc->replaceAllEnabled();
    case SearchingRole:
        return doc->searching();
    default:
        break;
    }

    return {};
}

QHash<int, QByteArray> WorkspaceController::roleNames() const
{
    return {
        { OccupiedRole, "occupied" },
        { TitleRole, "title" },
        { FilePathRole, "filePath" },
        { DirtyRole, "dirty" },
        { SavingRole, "saving" },
        { FocusedRole, "focused" },
        { CurrentLineRole, "currentLine" },
        { TotalLinesRole, "totalLines" },
        { PercentRole, "percent" },
        { CursorPositionRole, "cursorPosition" },
        { MultiSelectRole, "multiSelectEnabled" },
        { CanEditRole, "canEdit" },
        { CanUndoRole, "canUndo" },
        { CanRedoRole, "canRedo" },
        { TextLengthRole, "textLength" },
        { LargeFileRole, "largeFileMode" },
        { TextRevisionRole, "textRevision" },
        { SearchQueryRole, "searchQuery" },
        { MatchCountRole, "matchCount" },
        { MatchCountDisplayRole, "matchCountDisplay" },
        { CurrentMatchRole, "currentMatch" },
        { ReplaceAllEnabledRole, "replaceAllEnabled" },
        { SearchingRole, "searching" }
    };
}

int WorkspaceController::paneCount() const
{
    return occupiedCount();
}

int WorkspaceController::focusedPaneIndex() const
{
    return m_focusedPaneIndex;
}

bool WorkspaceController::canOpenMore() const
{
    return occupiedCount() < kMaxPaneCount;
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
    const int clamped = qBound(1024, bytes, 1024 * 1024);
    if (m_pasteLimitBytes == clamped) {
        return;
    }

    m_pasteLimitBytes = clamped;
    emit pasteLimitBytesChanged();
}

void WorkspaceController::newFile()
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时禁止修改内容。"));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    if (paneCount() == 0) {
        const int slot = firstEmptySlot();
        if (slot < 0) {
            return;
        }
        assignSessionToSlot(slot, m_documentManager.createUntitled());
        focusSlot(slot);
        return;
    }

    if (m_focusedPaneIndex < 0 || m_focusedPaneIndex >= paneCount()) {
        focusSlot(0);
    }

    if (!ensureCanDiscardSlot(m_focusedPaneIndex)) {
        return;
    }

    if (!ensureSlotEditableForContentChange(m_focusedPaneIndex)) {
        return;
    }

    assignSessionToSlot(m_focusedPaneIndex, m_documentManager.createUntitled());
    notifySlotChanged(m_focusedPaneIndex);
}

void WorkspaceController::openFile()
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时禁止打开文件。"));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(nullptr, QStringLiteral("打开文件"));
    if (path.isEmpty()) {
        return;
    }

    const int existingSlot = findSlotByPath(path);
    if (existingSlot >= 0) {
        focusSlot(existingSlot);
        emit toastRequested(QStringLiteral("该文件已打开，已定位到对应窗口。"));
        return;
    }

    if (paneCount() == 0) {
        const int slot = firstEmptySlot();
        if (slot < 0) {
            emit toastRequested(QStringLiteral("没有可用窗口。"));
            return;
        }
        startAsyncOpen(path, OpenMode::ReplaceFocused, slot);
        return;
    }

    if (m_focusedPaneIndex < 0 || m_focusedPaneIndex >= paneCount()) {
        focusSlot(0);
    }

    if (!ensureCanDiscardSlot(m_focusedPaneIndex)) {
        return;
    }

    if (!ensureSlotEditableForContentChange(m_focusedPaneIndex)) {
        return;
    }

    startAsyncOpen(path, OpenMode::ReplaceFocused, m_focusedPaneIndex);
}

void WorkspaceController::openMore()
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时禁止打开文件。"));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    if (occupiedCount() >= kMaxPaneCount) {
        emit toastRequested(QStringLiteral("最多只支持4个窗口。"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(nullptr, QStringLiteral("打开更多文件"));
    if (path.isEmpty()) {
        return;
    }

    const int existingSlot = findSlotByPath(path);
    if (existingSlot >= 0) {
        focusSlot(existingSlot);
        emit toastRequested(QStringLiteral("该文件已打开，已定位到对应窗口。"));
        return;
    }

    const int slot = firstEmptySlot();
    if (slot < 0) {
        emit toastRequested(QStringLiteral("没有可用窗口。"));
        return;
    }

    startAsyncOpen(path, OpenMode::AddMore, slot);
}

void WorkspaceController::closeFocused()
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时禁止关闭窗口。"));
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    if (m_focusedPaneIndex < 0 || m_focusedPaneIndex >= paneCount()) {
        return;
    }

    if (!m_slots.at(m_focusedPaneIndex).occupied) {
        return;
    }

    if (!ensureCanDiscardSlot(m_focusedPaneIndex)) {
        return;
    }

    if (m_slots.at(m_focusedPaneIndex).document && m_slots.at(m_focusedPaneIndex).document->isSaving()) {
        emit toastRequested(QStringLiteral("保存进行中，无法关闭该窗口。"));
        return;
    }

    closeSlot(m_focusedPaneIndex);
}

void WorkspaceController::saveFocused()
{
    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    if (m_focusedPaneIndex < 0 || m_focusedPaneIndex >= paneCount()) {
        return;
    }

    PaneSlot &slot = m_slots[m_focusedPaneIndex];
    if (!slot.occupied || !slot.document) {
        return;
    }

    if (slot.document->isSaving()) {
        emit toastRequested(QStringLiteral("保存进行中，请稍候。"));
        return;
    }
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时不可重复发起保存。"));
        return;
    }

    if (slot.document->filePath().isEmpty()) {
        saveFocusedAs();
        return;
    }

    slot.document->saveAsync();
    notifySlotChanged(m_focusedPaneIndex, { SavingRole, CanEditRole });
}

void WorkspaceController::saveFocusedAs()
{
    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    if (m_focusedPaneIndex < 0 || m_focusedPaneIndex >= paneCount()) {
        return;
    }

    PaneSlot &slot = m_slots[m_focusedPaneIndex];
    if (!slot.occupied || !slot.document) {
        return;
    }

    if (slot.document->isSaving()) {
        emit toastRequested(QStringLiteral("保存进行中，请稍候。"));
        return;
    }
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时不可重复发起保存。"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("另存为"), slot.document->filePath());
    if (path.isEmpty()) {
        return;
    }

    const int existingSlot = findSlotByPath(path);
    if (existingSlot >= 0 && existingSlot != m_focusedPaneIndex) {
        focusSlot(existingSlot);
        emit toastRequested(QStringLiteral("目标文件已在其他窗口打开，已定位。"));
        return;
    }

    slot.document->saveAsAsync(path);
    notifySlotChanged(m_focusedPaneIndex, { SavingRole, CanEditRole });
}

void WorkspaceController::setFocusedPane(int slot)
{
    focusSlot(slot);
}

void WorkspaceController::updateCursorPosition(int slot, int position)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return;
    }

    const int beforeCursor = pane.document->cursorPosition();
    const int beforeLine = pane.document->currentLine();
    pane.document->setCursorPosition(position);
    if (pane.document->cursorPosition() == beforeCursor
        && pane.document->currentLine() == beforeLine) {
        return;
    }
    notifySlotChanged(slot, { CursorPositionRole });
}

void WorkspaceController::setMultiSelectEnabled(int slot, bool enabled)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied) {
        return;
    }

    if (pane.multiSelectEnabled == enabled) {
        return;
    }

    pane.multiSelectEnabled = enabled;
    notifySlotChanged(slot, { MultiSelectRole });
}

bool WorkspaceController::canPaste(const QString &text) const
{
    return text.toUtf8().size() <= m_pasteLimitBytes;
}

QString WorkspaceController::clipboardText() const
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return {};
    }
    return clipboard->text();
}

void WorkspaceController::setClipboardText(const QString &text)
{
    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        return;
    }
    clipboard->setText(text);
}

void WorkspaceController::setSearchQuery(int slot, const QString &query)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return;
    }

    pane.document->setSearchQuery(query);
    notifySlotChanged(slot, { SearchQueryRole, MatchCountRole, MatchCountDisplayRole, CurrentMatchRole, ReplaceAllEnabledRole });
}

QString WorkspaceController::searchQueryAt(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return {};
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return {};
    }

    return pane.document->searchQuery();
}

int WorkspaceController::findNext(int slot)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return -1;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return -1;
    }

    const int position = pane.document->findNext();
    notifySlotChanged(slot, { CurrentMatchRole });
    return position;
}

int WorkspaceController::findPrevious(int slot)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return -1;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return -1;
    }

    const int position = pane.document->findPrevious();
    notifySlotChanged(slot, { CurrentMatchRole });
    return position;
}

int WorkspaceController::currentMatchPosition(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return -1;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return -1;
    }

    return pane.document->currentMatchPosition();
}

int WorkspaceController::queryLength(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return 0;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return 0;
    }

    return pane.document->queryLength();
}

bool WorkspaceController::replaceCurrent(int slot, const QString &replacement)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return false;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return false;
    }

    if (!pane.document->replaceCurrent(replacement)) {
        return false;
    }

    return true;
}

int WorkspaceController::replaceAll(int slot, const QString &replacement)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return 0;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return 0;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return 0;
    }

    const int replaced = pane.document->replaceAll(replacement);
    return replaced;
}

QString WorkspaceController::matchStatus(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return QStringLiteral("0/0");
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return QStringLiteral("0/0");
    }

    const QString total = pane.document->matchCountDisplay();
    const int current = pane.document->currentMatch();
    if (pane.document->matchCount() <= 0) {
        return QStringLiteral("0/%1").arg(total);
    }
    return QStringLiteral("%1/%2").arg(current).arg(total);
}

bool WorkspaceController::replaceAllEnabledAt(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    const PaneSlot &pane = m_slots.at(slot);
    return pane.occupied && pane.document && pane.document->replaceAllEnabled();
}

bool WorkspaceController::isSearchingAt(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    const PaneSlot &pane = m_slots.at(slot);
    return pane.occupied && pane.document && pane.document->searching();
}

bool WorkspaceController::isSlotOccupied(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }
    return m_slots.at(slot).occupied;
}

bool WorkspaceController::prepareForAppClose()
{
    if (m_openWatcher) {
        showWarning(QStringLiteral("正在打开文件，请稍后再退出。"));
        return false;
    }

    if (hasAnySavingSession()) {
        showWarning(QStringLiteral("有文件正在保存，请稍后再退出。"));
        return false;
    }

    for (int slot = occupiedCount() - 1; slot >= 0; --slot) {
        if (!ensureCanDiscardSlot(slot)) {
            return false;
        }
    }

    return true;
}

QString WorkspaceController::lineText(int slot, int zeroBasedLine) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return {};
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return {};
    }

    return pane.document->lineTextAt(zeroBasedLine);
}

int WorkspaceController::lineLength(int slot, int zeroBasedLine) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return 0;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return 0;
    }

    return pane.document->lineLengthAt(zeroBasedLine);
}

QString WorkspaceController::lineTextSlice(int slot, int zeroBasedLine, int startColumn, int maxChars) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return {};
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return {};
    }

    return pane.document->lineTextSliceAt(zeroBasedLine, startColumn, maxChars);
}

int WorkspaceController::lineStartOffset(int slot, int zeroBasedLine) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return 0;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return 0;
    }

    return pane.document->lineStartOffset(zeroBasedLine);
}

QString WorkspaceController::textSlice(int slot, int start, int length) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return {};
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return {};
    }

    return pane.document->textSlice(start, length);
}

int WorkspaceController::textLength(int slot) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return 0;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return 0;
    }

    return pane.document->textLength();
}

int WorkspaceController::lineForOffset(int slot, int offset) const
{
    if (slot < 0 || slot >= m_slots.size()) {
        return 0;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return 0;
    }

    return pane.document->lineForOffsetZeroBased(offset);
}

int WorkspaceController::applyTextEdit(int slot, int position, int removeLength, const QString &insertedText)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return -1;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return -1;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return -1;
    }

    const int newCursor = pane.document->applyTextEdit(position, removeLength, insertedText);
    if (newCursor < 0) {
        return -1;
    }

    return newCursor;
}

bool WorkspaceController::undoEdit(int slot)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return false;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return false;
    }

    if (!pane.document->undo()) {
        return false;
    }

    return true;
}

bool WorkspaceController::redoEdit(int slot)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return false;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return false;
    }

    if (!pane.document->redo()) {
        return false;
    }

    return true;
}

bool WorkspaceController::replaceLineText(int slot, int zeroBasedLine, const QString &lineText)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return false;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return false;
    }

    return pane.document->replaceLineText(zeroBasedLine, lineText);
}

bool WorkspaceController::deleteLineAt(int slot, int zeroBasedLine)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    PaneSlot &pane = m_slots[slot];
    if (!pane.occupied || !pane.document) {
        return false;
    }

    if (!ensureSlotEditableForContentChange(slot)) {
        notifySlotChanged(slot, { CanEditRole });
        return false;
    }

    return pane.document->deleteLineAt(zeroBasedLine);
}

QVector<int> WorkspaceController::searchMatchPositionsInRange(int slot,
                                                               int start,
                                                               int endExclusive,
                                                               int maxCount) const
{
    if (slot < 0 || slot >= m_slots.size() || maxCount <= 0) {
        return {};
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return {};
    }

    return pane.document->searchMatchPositionsInRange(start, endExclusive, maxCount);
}

int WorkspaceController::firstEmptySlot() const
{
    for (int i = 0; i < m_slots.size(); ++i) {
        if (!m_slots.at(i).occupied) {
            return i;
        }
    }
    return -1;
}

int WorkspaceController::occupiedCount() const
{
    int count = 0;
    for (const PaneSlot &slot : m_slots) {
        if (slot.occupied) {
            ++count;
        }
    }
    return count;
}

int WorkspaceController::findSlotByPath(const QString &path) const
{
    const QString normalized = PathUtils::normalizePath(path);
    if (normalized.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_slots.size(); ++i) {
        const PaneSlot &slot = m_slots.at(i);
        if (!slot.occupied || !slot.document) {
            continue;
        }

        if (PathUtils::normalizePath(slot.document->filePath()) == normalized) {
            return i;
        }
    }

    return -1;
}

int WorkspaceController::findSlotByDocument(DocumentSession *doc) const
{
    if (!doc) {
        return -1;
    }

    for (int i = 0; i < m_slots.size(); ++i) {
        const PaneSlot &slot = m_slots.at(i);
        if (slot.occupied && slot.document && slot.document.data() == doc) {
            return i;
        }
    }

    return -1;
}

bool WorkspaceController::hasAnySavingSession() const
{
    for (const PaneSlot &slot : m_slots) {
        if (slot.occupied && slot.document && slot.document->isSaving()) {
            return true;
        }
    }
    return false;
}

void WorkspaceController::notifyAllCanEditChanged()
{
    const int count = occupiedCount();
    for (int i = 0; i < count; ++i) {
        notifySlotChanged(i, { CanEditRole });
    }
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

bool WorkspaceController::ensureCanDiscardSlot(int slot)
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时禁止修改内容。"));
        return false;
    }

    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return true;
    }

    if (pane.document->isSaving()) {
        emit toastRequested(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    if (!pane.document->isDirty()) {
        return true;
    }

    QMessageBox messageBox;
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(QStringLiteral("未保存修改"));
    messageBox.setText(QStringLiteral("当前文档有未保存内容，是否先保存？"));
    const QPushButton *saveButton = messageBox.addButton(QStringLiteral("保存"), QMessageBox::AcceptRole);
    const QPushButton *discardButton = messageBox.addButton(QStringLiteral("不保存"), QMessageBox::DestructiveRole);
    messageBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    messageBox.exec();

    if (messageBox.clickedButton() == saveButton) {
        QString error;
        if (pane.document->filePath().isEmpty()) {
            const QString path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("保存文件"));
            if (path.isEmpty()) {
                return false;
            }
            const int existingSlot = findSlotByPath(path);
            if (existingSlot >= 0 && existingSlot != slot) {
                focusSlot(existingSlot);
                emit toastRequested(QStringLiteral("目标文件已在其他窗口打开，已定位。"));
                return false;
            }
            if (!pane.document->saveAsSync(path, &error)) {
                showWarning(error);
                return false;
            }
            notifySlotChanged(slot);
            return true;
        }

        if (!pane.document->saveSync(&error)) {
            showWarning(error);
            return false;
        }

        notifySlotChanged(slot);
        return true;
    }

    if (messageBox.clickedButton() == discardButton) {
        return true;
    }

    return false;
}

bool WorkspaceController::ensureSlotEditableForContentChange(int slot)
{
    if (hasAnySavingSession()) {
        emit toastRequested(QStringLiteral("有文件正在保存，暂时禁止修改内容。"));
        return false;
    }

    if (slot < 0 || slot >= m_slots.size()) {
        return false;
    }

    const PaneSlot &pane = m_slots.at(slot);
    if (!pane.occupied || !pane.document) {
        return true;
    }

    if (m_openWatcher
        && m_pendingOpen.mode == OpenMode::ReplaceFocused
        && m_pendingOpen.targetSlot == slot) {
        emit toastRequested(QStringLiteral("打开进行中，禁止修改该窗口内容。"));
        return false;
    }

    if (pane.document->isSaving()) {
        emit toastRequested(QStringLiteral("保存进行中，禁止修改文件内容。"));
        return false;
    }

    return true;
}

void WorkspaceController::startAsyncOpen(const QString &path, OpenMode mode, int targetSlot)
{
    if (path.isEmpty()) {
        return;
    }

    if (m_openWatcher) {
        emit toastRequested(QStringLiteral("正在打开文件，请稍候。"));
        return;
    }

    m_pendingOpen.mode = mode;
    m_pendingOpen.targetSlot = targetSlot;
    m_pendingOpen.path = path;

    if (mode == OpenMode::ReplaceFocused) {
        notifySlotChanged(targetSlot, { CanEditRole });
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

    emit toastRequested(QStringLiteral("正在打开文件，请稍候..."));
}

void WorkspaceController::handleAsyncOpenFinished(const DocumentSession::DecodedFileResult &result)
{
    const PendingOpenRequest request = m_pendingOpen;
    m_pendingOpen = PendingOpenRequest();

    if (request.mode == OpenMode::ReplaceFocused
        && request.targetSlot >= 0
        && request.targetSlot < paneCount()) {
        notifySlotChanged(request.targetSlot, { CanEditRole });
    }

    if (!result.ok) {
        showWarning(result.error);
        return;
    }

    const QString openedPath = result.path.isEmpty() ? request.path : result.path;
    const int existingSlot = findSlotByPath(openedPath);
    if (existingSlot >= 0) {
        focusSlot(existingSlot);
        emit toastRequested(QStringLiteral("该文件已打开，已定位到对应窗口。"));
        return;
    }

    int targetSlot = -1;
    if (request.mode == OpenMode::AddMore) {
        if (occupiedCount() >= kMaxPaneCount) {
            emit toastRequested(QStringLiteral("最多只支持4个窗口。"));
            return;
        }
        targetSlot = firstEmptySlot();
    } else {
        if (paneCount() == 0) {
            targetSlot = firstEmptySlot();
        } else if (request.targetSlot >= 0 && request.targetSlot < paneCount()) {
            targetSlot = request.targetSlot;
        } else if (m_focusedPaneIndex >= 0 && m_focusedPaneIndex < paneCount()) {
            targetSlot = m_focusedPaneIndex;
        } else {
            targetSlot = 0;
        }
    }

    if (targetSlot < 0) {
        emit toastRequested(QStringLiteral("没有可用窗口。"));
        return;
    }

    const QSharedPointer<DocumentSession> session = m_documentManager.createUntitled();
    session->applyLoadedFile(openedPath, result.text, result.codec);

    assignSessionToSlot(targetSlot, session);
    focusSlot(targetSlot);
}

void WorkspaceController::assignSessionToSlot(int slot, const QSharedPointer<DocumentSession> &session)
{
    if (slot < 0 || slot >= m_slots.size() || !session) {
        return;
    }

    const int countBefore = occupiedCount();
    PaneSlot &target = m_slots[slot];
    const bool isInsert = !target.occupied;

    if (isInsert) {
        beginInsertRows(QModelIndex(), slot, slot);
    }

    if (target.document) {
        QObject::disconnect(target.document.data(), nullptr, this, nullptr);
        m_documentManager.unregisterSession(target.document);
    }

    target.occupied = true;
    target.document = session;
    target.multiSelectEnabled = false;
    target.textRevision = 1;

    connectDocumentSignals(session);

    if (isInsert) {
        endInsertRows();
    } else {
        notifySlotChanged(slot);
    }

    if (occupiedCount() != countBefore) {
        emit paneCountChanged();
    }
    refreshAnySavingState();
}

void WorkspaceController::clearSlot(int slot)
{
    if (slot < 0 || slot >= m_slots.size()) {
        return;
    }

    PaneSlot &pane = m_slots[slot];
    if (pane.document) {
        QObject::disconnect(pane.document.data(), nullptr, this, nullptr);
        m_documentManager.unregisterSession(pane.document);
    }

    pane.occupied = false;
    pane.document.reset();
    pane.multiSelectEnabled = false;
    pane.textRevision = 0;

    notifySlotChanged(slot);
    refreshAnySavingState();
}

void WorkspaceController::closeSlot(int slot)
{
    const int countBefore = occupiedCount();
    if (slot < 0 || slot >= countBefore) {
        return;
    }

    const int previousFocused = m_focusedPaneIndex;
    beginRemoveRows(QModelIndex(), slot, slot);

    PaneSlot removed = m_slots.at(slot);
    if (removed.document) {
        QObject::disconnect(removed.document.data(), nullptr, this, nullptr);
        m_documentManager.unregisterSession(removed.document);
    }

    for (int i = slot; i < m_slots.size() - 1; ++i) {
        m_slots[i] = m_slots[i + 1];
    }

    m_slots[m_slots.size() - 1] = PaneSlot();

    const int countAfter = countBefore - 1;
    if (countAfter == 0) {
        m_focusedPaneIndex = -1;
    } else if (m_focusedPaneIndex == slot) {
        m_focusedPaneIndex = qMin(slot, countAfter - 1);
    } else if (m_focusedPaneIndex >= countAfter) {
        m_focusedPaneIndex = countAfter - 1;
    } else if (m_focusedPaneIndex > slot) {
        --m_focusedPaneIndex;
    }

    endRemoveRows();

    if (slot < countAfter) {
        emit dataChanged(index(slot, 0), index(countAfter - 1, 0));
    }

    emit paneCountChanged();
    if (previousFocused != m_focusedPaneIndex) {
        emit focusedPaneIndexChanged();
    }
    refreshAnySavingState();
}

void WorkspaceController::notifySlotChanged(int slot, const QVector<int> &roles)
{
    const int count = occupiedCount();
    if (slot < 0 || slot >= count) {
        return;
    }

    emit dataChanged(index(slot, 0), index(slot, 0), roles);
}

void WorkspaceController::connectDocumentSignals(const QSharedPointer<DocumentSession> &session)
{
    if (!session) {
        return;
    }

    DocumentSession *doc = session.data();

    QObject::connect(doc, &DocumentSession::textChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        if (slot < 0 || slot >= occupiedCount()) {
            return;
        }

        ++m_slots[slot].textRevision;
        notifySlotChanged(slot, { CursorPositionRole, TextLengthRole, LargeFileRole, TextRevisionRole, CanUndoRole, CanRedoRole });
    });

    QObject::connect(doc, &DocumentSession::dirtyChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        notifySlotChanged(slot, { DirtyRole, TitleRole });
    });

    QObject::connect(doc, &DocumentSession::filePathChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        notifySlotChanged(slot, { FilePathRole, TitleRole });
    });

    QObject::connect(doc, &DocumentSession::savingChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        notifySlotChanged(slot, { SavingRole });
        refreshAnySavingState();
        notifyAllCanEditChanged();
    });

    QObject::connect(doc, &DocumentSession::lineCountChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        notifySlotChanged(slot, { TotalLinesRole, PercentRole });
    });

    QObject::connect(doc, &DocumentSession::currentLineChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        notifySlotChanged(slot, { CurrentLineRole, PercentRole });
    });

    QObject::connect(doc, &DocumentSession::searchStateChanged, this, [this, doc]() {
        const int slot = findSlotByDocument(doc);
        notifySlotChanged(slot, { SearchQueryRole, MatchCountRole, MatchCountDisplayRole, CurrentMatchRole, ReplaceAllEnabledRole, SearchingRole });
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

void WorkspaceController::focusSlot(int slot)
{
    const int count = occupiedCount();
    if (slot < 0 || slot >= count) {
        return;
    }

    if (!m_slots.at(slot).occupied) {
        return;
    }

    if (m_focusedPaneIndex == slot) {
        return;
    }

    const int previous = m_focusedPaneIndex;
    m_focusedPaneIndex = slot;

    if (previous >= 0) {
        notifySlotChanged(previous, { FocusedRole });
    }
    notifySlotChanged(slot, { FocusedRole });
    emit focusedPaneIndexChanged();
}

void WorkspaceController::showWarning(const QString &message) const
{
    if (message.isEmpty()) {
        return;
    }

    QMessageBox::warning(nullptr, QStringLiteral("提示"), message);
}
