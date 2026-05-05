import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "components"

ApplicationWindow {
    id: window
    visible: true
    width: 1500
    height: 900
    title: "NCEditor"
    color: "#081222"
    onClosing: function(close) {
        if (!workspaceController.prepareForAppClose()) {
            close.accepted = false
        }
    }

    property int findSlotIndex: -1
    property bool pendingFindAutoSelect: false
    property bool workspaceLocked: workspaceController.anySaving || workspaceController.anyOpening

    function showToast(message) {
        if (!message || message.length === 0)
            return
        toastLabel.text = message
        toastRect.visible = true
        toastTimer.restart()
    }

    function paneItem(slot) {
        return paneRepeater.itemAt(slot)
    }

    function selectCurrentMatch(slot, position) {
        if (slot < 0) {
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
        var focused = workspaceController.focusedPaneIndex
        if (focused < 0)
            return
        flushPaneText(focused)
    }

    function flushPaneText(slot) {
        if (slot < 0)
            return
        var pane = paneItem(slot)
        if (pane && pane.flushPendingText) {
            pane.flushPendingText()
        }
    }

    function syncFindDialogSlot(slot, autoSelectCurrent) {
        pendingFindAutoSelect = false

        if (slot < 0 || !workspaceController.isSlotOccupied(slot)) {
            findSlotIndex = -1
            findDialog.setQueryTextSilently("")
            refreshFindDialogState()
            return
        }

        findSlotIndex = slot
        findDialog.setQueryTextSilently(workspaceController.searchQueryAt(slot))
        refreshFindDialogState()

        if (autoSelectCurrent && !workspaceController.isSearchingAt(slot)) {
            selectCurrentMatch(slot)
        }
    }

    function refreshFindDialogState() {
        if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
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
        target: workspaceController
        function onToastRequested(message) {
            showToast(message)
        }
        function onDataChanged(topLeft, bottomRight, roles) {
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
                    // Document session in the same slot may have been replaced (for example Open on focused slot).
                    // Re-sync query text/status to avoid stale find criteria shown in the dialog.
                    syncFindDialogSlot(findSlotIndex, false)
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
            if (findDialog.visible) {
                syncFindDialogSlot(workspaceController.focusedPaneIndex, true)
            }
        }
        function onPaneCountChanged() {
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

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: sidebar
            Layout.preferredWidth: 88
            Layout.fillHeight: true
            color: "#1a2539"
            border.width: 1
            border.color: "#2a3b57"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Button {
                    text: "新建"
                    Layout.fillWidth: true
                    enabled: !workspaceLocked
                    onClicked: {
                        flushFocusedPaneText()
                        workspaceController.newFile()
                    }
                }
                Button {
                    text: "打开"
                    Layout.fillWidth: true
                    enabled: !workspaceLocked
                    onClicked: {
                        flushFocusedPaneText()
                        workspaceController.openFile()
                    }
                }
                Button {
                    text: "打开更多"
                    Layout.fillWidth: true
                    enabled: workspaceController.canOpenMore && !workspaceLocked
                    onClicked: {
                        flushFocusedPaneText()
                        workspaceController.openMore()
                    }
                }
                Button {
                    text: "保存"
                    Layout.fillWidth: true
                    enabled: workspaceController.focusedPaneIndex >= 0 && !workspaceLocked
                    onClicked: {
                        flushFocusedPaneText()
                        workspaceController.saveFocused()
                    }
                }
                Button {
                    text: "另存为"
                    Layout.fillWidth: true
                    enabled: workspaceController.focusedPaneIndex >= 0 && !workspaceLocked
                    onClicked: {
                        flushFocusedPaneText()
                        workspaceController.saveFocusedAs()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#304564"
                }
                Button {
                    text: "关闭"
                    Layout.fillWidth: true
                    enabled: workspaceController.focusedPaneIndex >= 0 && !workspaceLocked
                    onClicked: {
                        flushFocusedPaneText()
                        workspaceController.closeFocused()
                    }
                }

                Item { Layout.fillHeight: true }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: "窗口: " + workspaceController.paneCount + "/4"
                    color: "#c6d7f5"
                    horizontalAlignment: Text.AlignHCenter
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: workspaceController.anyOpening
                        ? "状态: 打开中"
                        : (workspaceController.anySaving ? "状态: 保存中" : "状态: 就绪")
                    color: workspaceController.anyOpening
                        ? "#f5c469"
                        : (workspaceController.anySaving ? "#f5c469" : "#8fb0e2")
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0b162c"

            Rectangle {
                anchors.fill: parent
                visible: workspaceController.paneCount === 0
                color: "transparent"

                Label {
                    anchors.centerIn: parent
                    text: "当前无窗口，请点击“新建”或“打开”"
                    color: "#6e86ad"
                }
            }

            GridLayout {
                id: grid
                visible: workspaceController.paneCount > 0
                anchors.fill: parent
                anchors.margins: 6
                rows: workspaceController.paneCount <= 2 ? 1 : 2
                columns: workspaceController.paneCount <= 1 ? 1 : 2
                rowSpacing: 6
                columnSpacing: 6

                Repeater {
                    id: paneRepeater
                    model: workspaceController

                    EditorPane {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        slotIndex: index
                        occupied: model.occupied
                        title: model.title
                        filePath: model.filePath
                        dirty: model.dirty
                        saving: model.saving
                        focused: model.focused
                        currentLine: model.currentLine
                        totalLines: model.totalLines
                        percent: model.percent
                        multiSelectEnabled: model.multiSelectEnabled
                        canEdit: model.canEdit
                        canUndo: model.canUndo
                        canRedo: model.canRedo
                        cursorPosition: model.cursorPosition
                        textLength: model.textLength
                        largeFileMode: model.largeFileMode
                        textRevision: model.textRevision
                        searchQuery: model.searchQuery
                        matchCountDisplay: model.matchCountDisplay
                        currentMatch: model.currentMatch
                        replaceAllEnabled: model.replaceAllEnabled

                        onFocusRequested: workspaceController.setFocusedPane(slot)
                        onMultiSelectChanged: workspaceController.setMultiSelectEnabled(slot, enabled)
                        onFindRequested: {
                            workspaceController.setFocusedPane(slot)
                            flushPaneText(slot)
                            syncFindDialogSlot(slot, false)
                            findDialog.open()
                        }
                        onToast: showToast(message)
                    }
                }
            }
        }
    }

    FindReplaceDialog {
        id: findDialog
        x: Math.max(0, (window.width - width) / 2)
        y: Math.max(0, (window.height - height) / 2)

        onOpened: {
            if (workspaceController.focusedPaneIndex >= 0) {
                syncFindDialogSlot(workspaceController.focusedPaneIndex, true)
            }
        }
        onClosed: pendingFindAutoSelect = false

        onQueryChangedDebounced: function(query) {
            if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
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
            if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            var pos = workspaceController.findPrevious(findSlotIndex)
            refreshFindDialogState()
            selectCurrentMatch(findSlotIndex, pos)
        }

        onNextRequested: {
            if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
                return
            }
            flushPaneText(findSlotIndex)
            var pos = workspaceController.findNext(findSlotIndex)
            refreshFindDialogState()
            selectCurrentMatch(findSlotIndex, pos)
        }

        onReplaceRequested: function(replacement) {
            if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
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
            if (findSlotIndex < 0 || !workspaceController.isSlotOccupied(findSlotIndex)) {
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

    Rectangle {
        id: toastRect
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        color: "#1f2c45"
        border.color: "#3f5b87"
        border.width: 1
        radius: 6
        visible: false
        z: 100
        width: Math.min(parent.width * 0.7, toastLabel.implicitWidth + 26)
        height: toastLabel.implicitHeight + 16

        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: "#e8f0ff"
        }

        Timer {
            id: toastTimer
            interval: 2200
            repeat: false
            onTriggered: toastRect.visible = false
        }
    }
}
