import QtQuick 2.15
import QtQuick.Controls 2.15
import NCEditor 1.0

Item {
    id: root

    property var documentSession: null
    property bool editBlocked: false
    property int pasteLimitBytes: editorConfig.defaultPasteLimitBytes
    readonly property bool occupied: !!documentSession
    readonly property int totalLines: documentSession ? documentSession.lineCount : 0

    signal focusRequested()
    signal toast(string message)
    signal requestMenu(real x, real y)
    signal findRequested()

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

        documentSession: root.documentSession
        editBlocked: root.editBlocked
        pasteLimitBytes: root.pasteLimitBytes

        onToastRequested: root.toast(message)
        onFocusRequested: root.focusRequested()
        onMenuRequested: root.requestMenu(x, y)
        onFindRequested: root.findRequested()
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
