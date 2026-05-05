import QtQuick 2.15
import QtQuick.Controls 2.15
import NCEditor 1.0

Item {
    id: root

    property int slotIndex: -1
    property bool occupied: false
    property bool canEdit: false
    property int totalLines: 0
    property int currentLine: 0
    property int textRevision: 0
    property bool multiSelectEnabled: false
    property string searchQuery: ""
    readonly property real lastPaintMs: editor.lastPaintMs
    readonly property real averagePaintMs: editor.averagePaintMs
    readonly property int paintFps: editor.paintFps
    readonly property int visibleMatchCacheSize: editor.visibleMatchCacheSize

    signal focusRequested(int slot)
    signal toast(string message)
    signal requestMenu(real x, real y)
    signal findRequested(int slot)

    function selectByOffset(offset) {
        editor.selectByOffset(offset)
    }

    function selectRange(start, length) {
        editor.selectRange(start, length)
    }

    function performCopy() {
        editor.performCopy()
    }

    function performCut() {
        editor.performCut()
    }

    function performPaste() {
        editor.performPaste()
    }

    function performDelete() {
        editor.performDelete()
    }

    function performUndo() {
        editor.performUndo()
    }

    function performRedo() {
        editor.performRedo()
    }

    PaintedEditorItem {
        id: editor
        anchors.fill: parent
        anchors.rightMargin: vBar.visible ? vBar.width : 0
        anchors.bottomMargin: root.occupied ? hBar.height : 0

        controller: workspaceController
        slotIndex: root.slotIndex
        occupied: root.occupied
        canEdit: root.canEdit
        totalLines: root.totalLines
        currentLine: root.currentLine
        textRevision: root.textRevision
        multiSelectEnabled: root.multiSelectEnabled
        searchQuery: root.searchQuery
        perfStatsEnabled: !!perfOverlayEnabled

        onToastRequested: root.toast(message)
        onFocusRequested: root.focusRequested(slot)
        onMenuRequested: root.requestMenu(x, y)
        onFindRequested: root.findRequested(slot)
    }

    ScrollBar {
        id: vBar
        orientation: Qt.Vertical
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 12

        visible: root.occupied && root.totalLines > 0
        policy: ScrollBar.AlwaysOn
        active: true
        opacity: 1.0
        size: editor.scrollSize
        position: editor.scrollPosition * Math.max(0, 1 - size)
        onPositionChanged: {
            if (pressed) {
                var span = Math.max(0.000001, 1 - size)
                editor.scrollPosition = position / span
            }
        }

        background: Rectangle {
            radius: 6
            color: "#0b1d35"
            border.width: 1
            border.color: "#1f3557"
        }

        contentItem: Rectangle {
            radius: 6
            color: "#4c76b8"
            border.width: 1
            border.color: "#7ea4de"
        }
    }

    ScrollBar {
        id: hBar
        orientation: Qt.Horizontal
        anchors.left: parent.left
        anchors.right: vBar.visible ? vBar.left : parent.right
        anchors.bottom: parent.bottom
        height: 12

        visible: root.occupied && root.totalLines > 0
        enabled: editor.horizontalScrollSize < 0.999
        policy: ScrollBar.AlwaysOn
        active: true
        opacity: enabled ? 1.0 : 0.35
        size: editor.horizontalScrollSize
        position: editor.horizontalScrollPosition * Math.max(0, 1 - size)
        onPositionChanged: {
            if (pressed) {
                var span = Math.max(0.000001, 1 - size)
                editor.horizontalScrollPosition = position / span
            }
        }

        background: Rectangle {
            radius: 6
            color: "#0b1d35"
            border.width: 1
            border.color: "#1f3557"
        }

        contentItem: Rectangle {
            radius: 6
            color: "#4c76b8"
            border.width: 1
            border.color: "#7ea4de"
        }
    }
}
