import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property int slotIndex: -1
    property bool occupied: false
    property string title: ""
    property string filePath: ""
    property string backendText: ""
    property bool dirty: false
    property bool saving: false
    property bool focused: false
    property int currentLine: 0
    property int totalLines: 0
    property int percent: 0
    property bool multiSelectEnabled: false
    property var documentSession: null
    property bool canEdit: false
    property bool canUndo: documentSession ? documentSession.canUndo : false
    property bool canRedo: documentSession ? documentSession.canRedo : false
    property bool canModifySession: documentSession ? documentSession.canModify : false
    property int cursorPosition: 0
    property int textLength: 0
    property bool largeFileMode: false
    property int textRevision: 0
    property string searchQuery: ""
    property string matchCountDisplay: "0"
    property int currentMatch: 0
    property bool replaceAllEnabled: false

    signal focusRequested(int slot)
    signal textChangedByUser(int slot, string text)
    signal cursorMoved(int slot, int position)
    signal multiSelectChanged(int slot, bool enabled)
    signal findRequested(int slot)
    signal contextMenuRequested(int slot, real x, real y)
    signal toast(string message)

    color: "#0d1a2f"
    border.width: focused ? 2 : 1
    border.color: focused ? "#2f7df6" : "#2a3b57"
    radius: 4

    function selectRange(start, length) {
        if (!occupied || start < 0 || length <= 0) {
            return
        }
        editorViewport.selectRange(start, length)
    }

    function ensureEditable() {
        if (!canEdit || !canModifySession) {
            toast("保存进行中，禁止修改文件内容。")
            return false
        }
        return true
    }

    function mapEditorPointTo(item, x, y) {
        return editorViewport.mapToItem(item, x, y)
    }

    function performCopy() {
        if (!occupied) {
            return
        }
        editorViewport.performCopy()
    }

    function performCut() {
        if (!occupied || !ensureEditable()) {
            return
        }
        editorViewport.performCut()
    }

    function performDelete() {
        if (!occupied || !ensureEditable()) {
            return
        }
        editorViewport.performDelete()
    }

    function performPaste() {
        if (!occupied || !ensureEditable()) {
            return
        }
        editorViewport.performPaste()
    }

    function performUndo() {
        if (!occupied || !ensureEditable()) {
            return
        }
        editorViewport.performUndo()
    }

    function performRedo() {
        if (!occupied || !ensureEditable()) {
            return
        }
        editorViewport.performRedo()
    }

    function flushPendingText() {
        // No-op: editing is now fully handled by EditorViewport + C++ backend.
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: "#162743"
            border.width: 1
            border.color: "#223a61"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6
                Label {
                    text: root.occupied ? root.title : "空白窗口"
                    color: "#e7efff"
                    elide: Label.ElideMiddle
                    Layout.fillWidth: true
                }
                Label {
                    visible: root.largeFileMode && root.occupied
                    text: "EditorViewport"
                    color: "#f5c469"
                }
                Label {
                    text: root.saving ? "保存中" : ""
                    color: "#f5c469"
                }
            }
        }

        Rectangle {
            id: editorBody
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#0a1528"

            EditorViewport {
                id: editorViewport
                anchors.fill: parent
                anchors.margins: 2
                visible: root.occupied
                slotIndex: root.slotIndex
                occupied: root.occupied
                canEdit: root.canEdit
                totalLines: root.totalLines
                currentLine: root.currentLine
                textRevision: root.textRevision
                multiSelectEnabled: root.multiSelectEnabled
                searchQuery: root.searchQuery
                onFocusRequested: root.focusRequested(slot)
                onToast: root.toast(message)
                onRequestMenu: function(px, py) {
                    root.contextMenuRequested(root.slotIndex, px, py)
                }
                onFindRequested: root.findRequested(slot)
            }

            Label {
                anchors.centerIn: parent
                visible: !root.occupied
                text: "点击左侧按钮新建或打开文件"
                color: "#6e86ad"
            }

        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: "#13233f"
            border.width: 1
            border.color: "#223a61"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                Label {
                    text: root.occupied && root.totalLines > 0
                        ? root.currentLine + "/" + root.totalLines + " (" + root.percent + "%)"
                        : "0/0 (0%)"
                    color: "#d3e0f8"
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.multiSelectEnabled
                        ? "多选模式"
                        : (root.largeFileMode ? "大文件模式" : "标准模式")
                    color: root.multiSelectEnabled
                        ? "#3cd48f"
                        : (root.largeFileMode ? "#f5c469" : "#9fb5dc")
                }
                Label {
                    visible: !!perfOverlayEnabled && root.occupied
                    text: "FPS " + editorViewport.paintFps
                        + " | L " + editorViewport.lastPaintMs.toFixed(1) + "ms"
                        + " | A " + editorViewport.averagePaintMs.toFixed(1) + "ms"
                        + " | H " + editorViewport.visibleMatchCacheSize
                    color: "#6f86ac"
                    font.pixelSize: 11
                }
            }
        }
    }
}
