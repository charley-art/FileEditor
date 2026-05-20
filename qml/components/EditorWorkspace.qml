import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#0b162c"

    property var workspaceController: null
    property int findSlotIndex: -1
    property bool pendingFindAutoSelect: false
    property var findDocumentSession: null
    property var activeMenuDocumentSession: null
    property int floatingMenuOwnerSlot: -1

    signal toastRequested(string message)

    function showToast(message) {
        if (!message || message.length === 0) {
            return
        }
        root.toastRequested(message)
    }

    function paneItem(slot) {
        return paneRepeater.itemAt(slot)
    }

    function activeMenuPane() {
        if (floatingMenuOwnerSlot < 0 || !workspaceController) {
            return null
        }
        if (!workspaceController.isSlotOccupied(floatingMenuOwnerSlot)) {
            return null
        }
        return paneItem(floatingMenuOwnerSlot)
    }

    function closeFloatingMenu() {
        floatingMenu.closeMenu()
        floatingMenuOwnerSlot = -1
        activeMenuDocumentSession = null
    }

    function refreshFloatingMenuState() {
        var pane = activeMenuPane()
        if (!pane) {
            closeFloatingMenu()
            return
        }
        floatingMenu.canEdit = pane.canEdit
        if (workspaceController && floatingMenuOwnerSlot >= 0) {
            floatingMenu.canUndo = workspaceController.canUndoAt(floatingMenuOwnerSlot)
            floatingMenu.canRedo = workspaceController.canRedoAt(floatingMenuOwnerSlot)
        } else {
            floatingMenu.canUndo = pane.canUndo
            floatingMenu.canRedo = pane.canRedo
        }
    }

    function openFloatingMenuForPane(slot, x, y) {
        if (!workspaceController) {
            return
        }
        var pane = paneItem(slot)
        if (!pane || !pane.occupied) {
            return
        }

        if (floatingMenu.menuVisible && floatingMenuOwnerSlot !== slot) {
            floatingMenu.closeMenu()
        }

        workspaceController.setFocusedPane(slot)
        var mapped = pane.mapEditorPointTo(root, x, y)
        floatingMenuOwnerSlot = slot
        activeMenuDocumentSession = pane.documentSession
        floatingMenu.openAt(mapped.x, mapped.y, root.width, root.height)
        refreshFloatingMenuState()
    }

    function selectCurrentMatch(slot, position) {
        if (!workspaceController || slot < 0) {
            return
        }
        var pane = paneItem(slot)
        if (!pane) {
            return
        }
        var pos = position
        if (pos === undefined || pos < 0) {
            pos = workspaceController.currentMatchPosition(slot)
        }
        var len = workspaceController.queryLength(slot)
        if (pos >= 0 && len > 0) {
            pane.selectRange(pos, len)
        }
    }

    function flushFocusedPaneText() {
        if (!workspaceController) {
            return
        }
        var focused = workspaceController.focusedPaneIndex
        if (focused < 0) {
            return
        }
        flushPaneText(focused)
    }

    function flushPaneText(slot) {
        if (slot < 0) {
            return
        }
        var pane = paneItem(slot)
        if (pane && pane.flushPendingText) {
            pane.flushPendingText()
        }
    }

    function prepareForExternalAction() {
        closeFloatingMenu()
        flushFocusedPaneText()
    }

    function syncFindDialogSlot(slot, autoSelectCurrent) {
        pendingFindAutoSelect = false

        if (!workspaceController || slot < 0 || !workspaceController.isSlotOccupied(slot)) {
            findSlotIndex = -1
            findDocumentSession = null
            findDialog.setQueryTextSilently("")
            refreshFindDialogState()
            return
        }

        findSlotIndex = slot
        updateFindDocumentSession()
        findDialog.setQueryTextSilently(workspaceController.searchQueryAt(slot))
        refreshFindDialogState()

        if (autoSelectCurrent && !workspaceController.isSearchingAt(slot)) {
            selectCurrentMatch(slot)
        }
    }

    function updateFindDocumentSession() {
        if (!workspaceController
                || findSlotIndex < 0
                || !workspaceController.isSlotOccupied(findSlotIndex)) {
            findDocumentSession = null
            return
        }

        var pane = paneItem(findSlotIndex)
        findDocumentSession = pane ? pane.documentSession : null
    }

    function refreshFindDialogState() {
        if (!workspaceController
                || findSlotIndex < 0
                || !workspaceController.isSlotOccupied(findSlotIndex)) {
            findDialog.matchStatusText = "0/0"
            findDialog.replaceAllEnabled = false
            findDialog.searching = false
            findDialog.targetValid = false
            return
        }
        findDialog.matchStatusText = workspaceController.matchStatus(findSlotIndex)
        findDialog.replaceAllEnabled = workspaceController.replaceAllEnabledAt(findSlotIndex)
        findDialog.searching = workspaceController.isSearchingAt(findSlotIndex)
        findDialog.targetValid = true
    }

    Connections {
        target: root.findDocumentSession
        enabled: findDialog.visible && !!workspaceController && findSlotIndex >= 0

        function onSearchStateChanged() {
            refreshFindDialogState()
            if (pendingFindAutoSelect
                    && findSlotIndex >= 0
                    && !workspaceController.isSearchingAt(findSlotIndex)) {
                pendingFindAutoSelect = false
                selectCurrentMatch(findSlotIndex)
            }
        }
    }

    Connections {
        target: root.activeMenuDocumentSession
        enabled: floatingMenu.menuVisible && !!workspaceController && floatingMenuOwnerSlot >= 0

        function onEditCapabilitiesChanged() {
            refreshFloatingMenuState()
        }
    }

    Connections {
        target: workspaceController
        enabled: !!workspaceController

        function onToastRequested(message) {
            showToast(message)
        }
        function onAnySavingChanged() {
            if (workspaceController.anySaving && floatingMenu.menuVisible) {
                closeFloatingMenu()
            }
        }
        function onAnyOpeningChanged() {
            if (workspaceController.anyOpening && floatingMenu.menuVisible) {
                closeFloatingMenu()
            }
        }
        function onDataChanged(topLeft, bottomRight, roles) {
            refreshFloatingMenuState()

            if (findDialog.visible) {
                if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
                    syncFindDialogSlot(workspaceController.focusedPaneIndex, false)
                    if (findSlotIndex < 0) {
                        return
                    }
                }

                if (findSlotIndex >= topLeft.row
                        && findSlotIndex <= bottomRight.row
                        && (!roles || roles.length === 0)) {
                    syncFindDialogSlot(findSlotIndex, false)
                } else {
                    updateFindDocumentSession()
                }
                if (floatingMenu.menuVisible) {
                    var pane = activeMenuPane()
                    activeMenuDocumentSession = pane ? pane.documentSession : null
                }
                refreshFindDialogState()
                if (pendingFindAutoSelect
                        && findSlotIndex >= 0
                        && !workspaceController.isSearchingAt(findSlotIndex)) {
                    pendingFindAutoSelect = false
                    selectCurrentMatch(findSlotIndex)
                }
            }
        }
        function onFocusedPaneIndexChanged() {
            if (floatingMenu.menuVisible
                    && floatingMenuOwnerSlot >= 0
                    && floatingMenuOwnerSlot !== workspaceController.focusedPaneIndex) {
                closeFloatingMenu()
            }
            if (findDialog.visible) {
                syncFindDialogSlot(workspaceController.focusedPaneIndex, true)
            }
        }
        function onPaneCountChanged() {
            if (floatingMenu.menuVisible) {
                closeFloatingMenu()
            }

            if (!findDialog.visible) {
                return
            }

            if (workspaceController.paneCount <= 0) {
                findDialog.close()
                syncFindDialogSlot(-1, false)
                return
            }

            if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
                syncFindDialogSlot(workspaceController.focusedPaneIndex, true)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !!workspaceController && workspaceController.paneCount === 0
        color: "transparent"

        Label {
            anchors.centerIn: parent
            text: "当前无窗口，请点击“新建”或“打开”"
            color: "#6e86ad"
        }
    }

    GridLayout {
        id: grid
        visible: !!workspaceController && workspaceController.paneCount > 0
        anchors.fill: parent
        anchors.margins: 6
        rows: workspaceController && workspaceController.paneCount <= 2 ? 1 : 2
        columns: workspaceController && workspaceController.paneCount <= 1 ? 1 : 2
        rowSpacing: 6
        columnSpacing: 6

        Repeater {
            id: paneRepeater
            model: workspaceController ? workspaceController : 0

            EditorPane {
                Layout.fillWidth: true
                Layout.fillHeight: true

                slotIndex: index
                occupied: model.occupied
                title: documentSession
                    ? (documentSession.dirty
                       ? documentSession.displayPath + " *"
                       : documentSession.displayPath)
                    : "空白窗口"
                saving: documentSession ? documentSession.saving : false
                focused: model.focused
                currentLine: documentSession ? documentSession.currentLine : 0
                totalLines: documentSession ? documentSession.lineCount : 0
                percent: documentSession ? documentSession.currentLinePercent : 0
                multiSelectEnabled: model.multiSelectEnabled
                documentSession: model.documentSession
                canEdit: documentSession ? documentSession.canModify : false
                largeFileMode: documentSession ? documentSession.largeFileMode : false
                textRevision: documentSession ? documentSession.textRevision : 0
                searchQuery: documentSession ? documentSession.searchQuery : ""

                onFocusRequested: {
                    if (floatingMenu.menuVisible && floatingMenuOwnerSlot !== slot) {
                        closeFloatingMenu()
                    }
                    workspaceController.setFocusedPane(slot)
                }
                onMultiSelectChanged: workspaceController.setMultiSelectEnabled(slot, enabled)
                onFindRequested: {
                    workspaceController.setFocusedPane(slot)
                    flushPaneText(slot)
                    syncFindDialogSlot(slot, false)
                    findDialog.open()
                }
                onContextMenuRequested: function(slot, x, y) {
                    openFloatingMenuForPane(slot, x, y)
                }
                onToast: showToast(message)
            }
        }
    }

    FloatingMenu {
        id: floatingMenu
        parent: root
        z: 200

        onSelectRequested: {
            var pane = activeMenuPane()
            if (pane) {
                workspaceController.setMultiSelectEnabled(pane.slotIndex, !pane.multiSelectEnabled)
            }
        }
        onCopyRequested: {
            var pane = activeMenuPane()
            if (pane) {
                pane.performCopy()
            }
        }
        onCutRequested: {
            var pane = activeMenuPane()
            if (pane) {
                pane.performCut()
            }
        }
        onPasteRequested: {
            var pane = activeMenuPane()
            if (pane) {
                pane.performPaste()
            }
        }
        onDeleteRequested: {
            var pane = activeMenuPane()
            if (pane) {
                pane.performDelete()
            }
        }
        onUndoRequested: {
            var pane = activeMenuPane()
            if (pane) {
                pane.performUndo()
            }
        }
        onRedoRequested: {
            var pane = activeMenuPane()
            if (pane) {
                pane.performRedo()
            }
        }
        onFindRequested: {
            var pane = activeMenuPane()
            if (!pane) {
                return
            }
            workspaceController.setFocusedPane(pane.slotIndex)
            flushPaneText(pane.slotIndex)
            syncFindDialogSlot(pane.slotIndex, false)
            findDialog.open()
        }
        onCloseRequested: {
            closeFloatingMenu()
        }
    }

    FindReplaceDialog {
        id: findDialog
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)

        onOpened: {
            if (workspaceController && workspaceController.focusedPaneIndex >= 0) {
                syncFindDialogSlot(workspaceController.focusedPaneIndex, true)
            }
        }
        onClosed: pendingFindAutoSelect = false

        onQueryChangedDebounced: function(query) {
            if (!workspaceController
                    || findSlotIndex < 0
                    || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            pendingFindAutoSelect = true
            workspaceController.setSearchQuery(findSlotIndex, query)
            refreshFindDialogState()
            if (!workspaceController.isSearchingAt(findSlotIndex)) {
                pendingFindAutoSelect = false
                selectCurrentMatch(findSlotIndex)
            }
        }

        onPreviousRequested: {
            if (!workspaceController
                    || findSlotIndex < 0
                    || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            var pos = workspaceController.findPrevious(findSlotIndex)
            refreshFindDialogState()
            selectCurrentMatch(findSlotIndex, pos)
        }

        onNextRequested: {
            if (!workspaceController
                    || findSlotIndex < 0
                    || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            var pos = workspaceController.findNext(findSlotIndex)
            refreshFindDialogState()
            selectCurrentMatch(findSlotIndex, pos)
        }

        onReplaceRequested: function(replacement) {
            if (!workspaceController
                    || findSlotIndex < 0
                    || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            if (!workspaceController.replaceCurrent(findSlotIndex, replacement)) {
                showToast("当前无法替换")
                return
            }
            refreshFindDialogState()
            selectCurrentMatch(findSlotIndex)
        }

        onReplaceAllRequested: function(replacement) {
            if (!workspaceController
                    || findSlotIndex < 0
                    || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            var replaced = workspaceController.replaceAll(findSlotIndex, replacement)
            if (replaced <= 0) {
                showToast("全部替换不可用或无匹配")
                return
            }
            showToast("已替换 " + replaced + " 处")
            refreshFindDialogState()
            selectCurrentMatch(findSlotIndex)
        }
    }
}
