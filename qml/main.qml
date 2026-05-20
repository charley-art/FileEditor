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

    property var wsController: workspaceController
    property bool workspaceLocked: workspaceController.anySaving || workspaceController.anyOpening

    onClosing: function(close) {
        if (!workspaceController.prepareForAppClose()) {
            close.accepted = false
        }
    }

    function showToast(message) {
        if (!message || message.length === 0) {
            return
        }
        toastLabel.text = message
        toastRect.visible = true
        toastTimer.restart()
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
                        editorWorkspace.prepareForExternalAction()
                        workspaceController.newFile()
                    }
                }
                Button {
                    text: "打开"
                    Layout.fillWidth: true
                    enabled: !workspaceLocked
                    onClicked: {
                        editorWorkspace.prepareForExternalAction()
                        workspaceController.openFile()
                    }
                }
                Button {
                    text: "打开更多"
                    Layout.fillWidth: true
                    enabled: workspaceController.canOpenMore && !workspaceLocked
                    onClicked: {
                        editorWorkspace.prepareForExternalAction()
                        workspaceController.openMore()
                    }
                }
                Button {
                    text: "保存"
                    Layout.fillWidth: true
                    enabled: workspaceController.focusedPaneIndex >= 0 && !workspaceLocked
                    onClicked: {
                        editorWorkspace.prepareForExternalAction()
                        workspaceController.saveFocused()
                    }
                }
                Button {
                    text: "另存为"
                    Layout.fillWidth: true
                    enabled: workspaceController.focusedPaneIndex >= 0 && !workspaceLocked
                    onClicked: {
                        editorWorkspace.prepareForExternalAction()
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
                        editorWorkspace.prepareForExternalAction()
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

        EditorWorkspace {
            id: editorWorkspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            workspaceController: window.wsController
            onToastRequested: window.showToast(message)
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
