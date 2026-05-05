import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Popup {
    id: root

    property string matchStatusText: "0/0"
    property bool replaceAllEnabled: false
    property bool replaceVisible: true
    property bool searching: false
    property bool targetValid: false
    property bool suppressQueryDebounce: false

    property alias queryText: queryField.text
    property alias replacementText: replaceField.text

    signal queryChangedDebounced(string query)
    signal previousRequested()
    signal nextRequested()
    signal replaceRequested(string replacement)
    signal replaceAllRequested(string replacement)

    modal: false
    focus: true
    width: 360
    height: replaceCheck.checked ? 246 : 172
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#16243a"
        radius: 8
        border.width: 1
        border.color: "#19a974"
    }

    Timer {
        id: queryDebounce
        interval: 150
        repeat: false
        onTriggered: root.queryChangedDebounced(queryField.text)
    }

    function setQueryTextSilently(text) {
        suppressQueryDebounce = true
        queryDebounce.stop()
        queryField.text = text ? text : ""
        suppressQueryDebounce = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "查找"
                color: "#ffffff"
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: "✕"
                onClicked: root.close()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "查找内容"
                color: "#dce6f7"
                Layout.preferredWidth: 70
            }
            TextField {
                id: queryField
                Layout.fillWidth: true
                selectByMouse: true
                onTextChanged: {
                    if (!root.suppressQueryDebounce) {
                        queryDebounce.restart()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: root.searching
                    ? "查找结果: 查找中..."
                    : ("查找结果: " + root.matchStatusText)
                color: "#c8d6f5"
                Layout.fillWidth: true
            }
            Button {
                text: "上一个"
                enabled: root.targetValid && !root.searching && queryField.text.length > 0
                onClicked: root.previousRequested()
            }
            Button {
                text: "下一个"
                enabled: root.targetValid && !root.searching && queryField.text.length > 0
                onClicked: root.nextRequested()
            }
        }

        CheckBox {
            id: replaceCheck
            text: "替换"
            checked: root.replaceVisible
            onToggled: root.replaceVisible = checked
        }

        RowLayout {
            Layout.fillWidth: true
            visible: replaceCheck.checked
            spacing: 8
            Label {
                text: "替换成"
                color: "#dce6f7"
                Layout.preferredWidth: 70
            }
            TextField {
                id: replaceField
                Layout.fillWidth: true
                selectByMouse: true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: replaceCheck.checked
            spacing: 8
            Button {
                text: "替换"
                enabled: root.targetValid && !root.searching && queryField.text.length > 0
                onClicked: root.replaceRequested(replaceField.text)
            }
            Button {
                text: "全部替换"
                enabled: root.targetValid && !root.searching && root.replaceAllEnabled
                onClicked: root.replaceAllRequested(replaceField.text)
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "取消"
                onClicked: root.close()
            }
        }
    }
}
