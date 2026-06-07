import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import NCEditor 1.0
Rectangle {
    id: root

    property bool focused: false
    property DocumentSession documentSession: null
    property bool editBlocked: false
    property int pasteLimitBytes: editorConfig.defaultPasteLimitBytes
    readonly property bool canEdit: documentSession ? documentSession.canModify && !editBlocked : false
    readonly property bool canUndo: documentSession ? documentSession.canUndo && !editBlocked : false
    readonly property bool canRedo: documentSession ? documentSession.canRedo && !editBlocked : false
    readonly property string title: documentSession ? documentSession.displayPath : qsTr("Untitled")
    readonly property bool saving: documentSession ? documentSession.saving : false

    signal focusRequested()
    signal findRequested()
    signal contextMenuRequested(real x, real y)
    signal toast(string message)

    color: "#0d1a2f"
    border.width: focused ? 2 : 1
    border.color: focused ? "#2f7df6" : "#2a3b57"
    radius: 4

    function selectRange(start, length) {
        if (!documentSession || start < 0 || length <= 0) {
            return
        }
        editorViewport.selectRange(start, length)
    }

    function ensureEditable() {
        if (!canEdit) {
            toast("Saving is in progress. Editing is disabled.")
            return false
        }
        return true
    }

    function mapEditorPointTo(item, x, y) {
        return editorViewport.mapToItem(item, x, y)
    }

    function performCopy() {
        if (!documentSession) {
            return
        }
        editorViewport.performCopy()
    }

    function performCut() {
        if (!documentSession || !ensureEditable()) {
            return
        }
        editorViewport.performCut()
    }

    function performDelete() {
        if (!documentSession || !ensureEditable()) {
            return
        }
        editorViewport.performDelete()
    }

    function performPaste() {
        if (!documentSession || !ensureEditable()) {
            return
        }
        editorViewport.performPaste()
    }

    function performUndo() {
        if (!documentSession || !ensureEditable()) {
            return
        }
        editorViewport.performUndo()
    }

    function performRedo() {
        if (!documentSession || !ensureEditable()) {
            return
        }
        editorViewport.performRedo()
    }

    function toggleMultiSelectedEnabled()
    {
        if(!documentSession || !ensureEditable())
        {
            documentSession.toggleMultiSelectEnabled();
        }
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
                    text: root.documentSession ? root.documentSession.displayPath : qsTr("Untitled")
                    color: "#e7efff"
                    elide: Label.ElideMiddle
                    Layout.fillWidth: true
                }
                Label {
                    text: root.saving ? "Saving" : ""
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
                visible: root.documentSession
                documentSession: root.documentSession
                editBlocked: root.editBlocked
                pasteLimitBytes: root.pasteLimitBytes
                onFocusRequested: root.focusRequested()
                onToast: root.toast(message)
                onRequestMenu: function(px, py) {
                    root.contextMenuRequested(px, py)
                }
                onFindRequested: root.findRequested()
            }

            Label {
                anchors.centerIn: parent
                visible: !root.documentSession
                text: "Use the sidebar to create or open a file"
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
                    text: root.documentSession
                        ? formattedCurrentLine(root.documentSession.currentLine, root.documentSession.lineCount)
                        : "0/0 (0%)"
                    color: "#d3e0f8"

                    function formattedCurrentLine(currentLine, totalLines)
                    {
                        if (currentLine <= 0 || totalLines <= 0)
                        {
                            return "0/0 (0%)";
                        }

                        var percentage = Math.round(currentLine * 100 / totalLines);
                        percentage = Math.min(percentage, 100);

                        return currentLine + "/" + totalLines + " (" + percentage + "%)";
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
