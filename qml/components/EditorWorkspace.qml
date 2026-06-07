import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import NCEditor 1.0

Rectangle {
    id: root
    color: "#0b162c"

    property var workspaceController: null
    property var focusedPane: null
    property bool pendingFindAutoSelect: false
    readonly property DocumentSession focusedDocumentSession: focusedPane ? focusedPane.documentSession : null

    signal toastRequested(string message)

    function showToast(message) {
        if (!message || message.length === 0) {
            return
        }
        root.toastRequested(message)
    }

    function paneForSessionId(sessionId) {
        if (sessionId <= 0) {
            return null
        }
        for (var i = 0; i < paneRepeater.count; ++i) {
            var pane = paneRepeater.itemAt(i)
            if (pane && pane.documentSession && pane.documentSession.sessionId === sessionId) {
                return pane
            }
        }
        return null
    }

    function syncFocusedPaneFromController() {
        if (!workspaceController || workspaceController.focusedSessionId <= 0) {
            focusedPane = null
            pendingFindAutoSelect = false
            closeFloatingMenu()
            if (findDialog.visible) {
                findDialog.close()
            }
            return
        }

        var pane = paneForSessionId(workspaceController.focusedSessionId)
        if (!pane) {
            focusedPane = null
            Qt.callLater(syncFocusedPaneFromController)
            return
        }

        focusedPane = pane
        closeFloatingMenu()
        syncFindDialogFromFocusedPane(findDialog.visible)
    }

    function focusPane(pane) {
        if (!workspaceController || !pane || !pane.documentSession) {
            return
        }
        focusedPane = pane
        workspaceController.focusSession(pane.documentSession.sessionId)
    }

    function closeFloatingMenu() {
        floatingMenu.closeMenu()
    }

    function prepareForExternalAction() {
        closeFloatingMenu()
    }

    function openFloatingMenuForPane(pane, x, y) {
        if (!pane || !pane.documentSession) {
            return
        }

        focusPane(pane)
        var mapped = pane.mapEditorPointTo(root, x, y)
        floatingMenu.openAt(mapped.x, mapped.y, root.width, root.height)
    }

    function openFindForPane(pane) {
        if (!pane || !pane.documentSession) {
            return
        }
        focusPane(pane)
        syncFindDialogFromFocusedPane(true)
        findDialog.open()
    }

    function formatMatchStatus(doc) {
        if (!doc) {
            return "0/0"
        }
        return doc.matchCount > 0
            ? doc.currentMatch + "/" + doc.matchCountDisplay
            : "0/" + doc.matchCountDisplay
    }

    function syncFindDialogFromFocusedPane(autoSelectCurrent) {
        pendingFindAutoSelect = false
        findDialog.setQueryTextSilently(focusedDocumentSession ? focusedDocumentSession.searchQuery : "")
        if (!autoSelectCurrent || !focusedDocumentSession) {
            return
        }
        if (focusedDocumentSession.searching) {
            pendingFindAutoSelect = true
        } else {
            selectCurrentMatch()
        }
    }

    function selectMatch(position) {
        var doc = focusedDocumentSession
        if (!focusedPane || !doc) {
            return
        }
        var len = doc.queryLength()
        if (position >= 0 && len > 0) {
            focusedPane.selectRange(position, len)
        }
    }

    function selectCurrentMatch() {
        if (!focusedDocumentSession) {
            return
        }
        selectMatch(focusedDocumentSession.currentMatchPosition())
    }

    Connections {
        target: root.focusedDocumentSession

        function onSearchStateChanged() {
            if (pendingFindAutoSelect
                    && root.focusedDocumentSession
                    && !root.focusedDocumentSession.searching) {
                pendingFindAutoSelect = false
                selectCurrentMatch()
            }
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

        function onFocusedSessionIdChanged() {
            syncFocusedPaneFromController()
        }

        function onSessionCountChanged() {
            Qt.callLater(syncFocusedPaneFromController)
            if (workspaceController.sessionCount <= 0) {
                findDialog.close()
                pendingFindAutoSelect = false
                closeFloatingMenu()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !!workspaceController && workspaceController.sessionCount === 0
        color: "transparent"

        Label {
            anchors.centerIn: parent
            text: "No panes are open. Click New or Open to get started."
            color: "#6e86ad"
        }
    }

    GridLayout {
        id: grid
        visible: !!workspaceController && workspaceController.sessionCount > 0
        anchors.fill: parent
        anchors.margins: 6
        rows: workspaceController && workspaceController.sessionCount <= 2 ? 1 : 2
        columns: workspaceController && workspaceController.sessionCount <= 1 ? 1 : 2
        rowSpacing: 6
        columnSpacing: 6

        Repeater {
            id: paneRepeater
            model: workspaceController ? workspaceController : 0

            EditorPane {
                id: editorPane
                Layout.fillWidth: true
                Layout.fillHeight: true

                focused: !!workspaceController
                    && !!documentSession
                    && documentSession.sessionId === workspaceController.focusedSessionId
                documentSession: model.documentSession
                editBlocked: !!workspaceController && workspaceController.anyOpening
                pasteLimitBytes: workspaceController
                    ? workspaceController.pasteLimitBytes
                    : editorConfig.defaultPasteLimitBytes

                Component.onCompleted: {
                    if (focused) {
                        root.focusedPane = editorPane
                    }
                }

                onFocusRequested: {
                    if (floatingMenu.menuVisible && root.focusedPane !== editorPane) {
                        closeFloatingMenu()
                    }
                    root.focusPane(editorPane)
                }

                onFindRequested: root.openFindForPane(editorPane)

                onContextMenuRequested: function(x, y) {
                    root.openFloatingMenuForPane(editorPane, x, y)
                }

                onToast: root.showToast(message)
            }
        }
    }

    FloatingMenu {
        id: floatingMenu
        parent: root
        z: 200
        canUndo: root.focusedDocumentSession ? root.focusedDocumentSession.canUndo : false
        canRedo: root.focusedDocumentSession ? root.focusedDocumentSession.canRedo : false
        canEdit: root.focusedPane ? root.focusedPane.canEdit : false

        onSelectRequested: {
            if (root.focusedDocumentSession) {
                root.focusedDocumentSession.toggleMultiSelectEnabled()
            }
        }

        onCopyRequested: {
            if (root.focusedPane) {
                root.focusedPane.performCopy()
            }
        }

        onCutRequested: {
            if (root.focusedPane) {
                root.focusedPane.performCut()
            }
        }

        onPasteRequested: {
            if (root.focusedPane) {
                root.focusedPane.performPaste()
            }
        }

        onDeleteRequested: {
            if (root.focusedPane) {
                root.focusedPane.performDelete()
            }
        }

        onUndoRequested: {
            if (root.focusedPane) {
                root.focusedPane.performUndo()
            }
        }

        onRedoRequested: {
            if (root.focusedPane) {
                root.focusedPane.performRedo()
            }
        }

        onFindRequested: {
            if (root.focusedPane) {
                root.openFindForPane(root.focusedPane)
                closeFloatingMenu()
            }
        }

        onCloseRequested: closeFloatingMenu()
    }

    FindReplaceDialog {
        id: findDialog
        x: Math.max(0, (root.width - width) / 2)
        y: Math.max(0, (root.height - height) / 2)
        hasDocument: !!root.focusedDocumentSession
        searching: root.focusedDocumentSession ? root.focusedDocumentSession.searching : false
        replaceAllEnabled: root.focusedDocumentSession ? root.focusedDocumentSession.replaceAllEnabled : false
        matchStatusText: root.formatMatchStatus(root.focusedDocumentSession)
        operationBlocked: !!workspaceController && workspaceController.anyOpening

        onOpened: syncFindDialogFromFocusedPane(true)
        onClosed: pendingFindAutoSelect = false

        onQueryChangedDebounced: function(query) {
            if (!root.focusedDocumentSession) {
                return
            }
            pendingFindAutoSelect = true
            root.focusedDocumentSession.setSearchQuery(query)
            if (!root.focusedDocumentSession.searching) {
                pendingFindAutoSelect = false
                selectCurrentMatch()
            }
        }

        onPreviousRequested: {
            if (root.focusedDocumentSession) {
                selectMatch(root.focusedDocumentSession.findPrevious())
            }
        }

        onNextRequested: {
            if (root.focusedDocumentSession) {
                selectMatch(root.focusedDocumentSession.findNext())
            }
        }

        onReplaceRequested: function(replacement) {
            var doc = root.focusedDocumentSession
            if (!doc || findDialog.operationBlocked || !doc.canModify) {
                showToast("Replace is not available right now.")
                return
            }
            if (!doc.replaceCurrent(replacement)) {
                showToast("Cannot replace the current match.")
                return
            }
            selectCurrentMatch()
        }

        onReplaceAllRequested: function(replacement) {
            var doc = root.focusedDocumentSession
            if (!doc || findDialog.operationBlocked || !doc.canModify) {
                showToast("Replace is not available right now.")
                return
            }
            var replaced = doc.replaceAll(replacement)
            if (replaced <= 0) {
                showToast("Replace all is unavailable or there are no matches.")
                return
            }
            showToast("Replaced " + replaced + " match(es).")
            selectCurrentMatch()
        }
    }
}
