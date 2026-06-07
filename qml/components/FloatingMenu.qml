import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root

    property bool menuVisible: false
    property bool canUndo: false
    property bool canRedo: false
    property bool canEdit: false

    signal selectRequested()
    signal copyRequested()
    signal cutRequested()
    signal pasteRequested()
    signal deleteRequested()
    signal undoRequested()
    signal redoRequested()
    signal findRequested()
    signal closeRequested()

    visible: menuVisible
    color: "#2a3245"
    border.color: "#44506b"
    border.width: 1
    radius: 6
    z: 20

    width: actionsRow.implicitWidth + 16
    height: 58

    function openAt(px, py, boundsWidth, boundsHeight) {
        menuVisible = true
        x = px - width / 2
        y = py - height / 2
        clamp(boundsWidth, boundsHeight)
    }

    function closeMenu() {
        menuVisible = false
    }

    function clamp(boundsWidth, boundsHeight) {
        x = Math.max(0, Math.min(x, boundsWidth - width))
        y = Math.max(0, Math.min(y, boundsHeight - height))
    }

    onXChanged: {
        if (parent) {
            clamp(parent.width, parent.height)
        }
    }

    onYChanged: {
        if (parent) {
            clamp(parent.width, parent.height)
        }
    }

    Rectangle {
        id: dragHandle
        width: parent.width
        height: 12
        color: "transparent"

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            drag.target: root
            drag.minimumX: 0
            drag.minimumY: 0
            drag.maximumX: root.parent ? root.parent.width - root.width : 0
            drag.maximumY: root.parent ? root.parent.height - root.height : 0
        }
    }

    RowLayout {
        id: actionsRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.bottomMargin: 6
        spacing: 6

        ToolButton {
            text: "Select"
            onClicked: root.selectRequested()
        }
        ToolButton {
            text: "Copy"
            onClicked: root.copyRequested()
        }
        ToolButton {
            text: "Cut"
            enabled: root.canEdit
            onClicked: root.cutRequested()
        }
        ToolButton {
            text: "Paste"
            enabled: root.canEdit
            onClicked: root.pasteRequested()
        }
        ToolButton {
            text: "Delete"
            enabled: root.canEdit
            onClicked: root.deleteRequested()
        }
        ToolButton {
            text: "Undo"
            enabled: root.canEdit && root.canUndo
            onClicked: root.undoRequested()
        }
        ToolButton {
            text: "Redo"
            enabled: root.canEdit && root.canRedo
            onClicked: root.redoRequested()
        }
        ToolButton {
            text: "Find"
            onClicked: root.findRequested()
        }
        ToolButton {
            text: "Close"
            onClicked: root.closeRequested()
        }
    }
}
