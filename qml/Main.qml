import QtQuick
import QtWebView

// Thin native shell: one full-screen WebView that shows the HTML interface served by the C++ backend.
Window {
    id: root
    visible: true
    width: 360
    height: 780
    title: "Flood Monitor"
    color: "#0b1220"

    WebView {
        id: web
        anchors.fill: parent
        url: startUrl

        onLoadingChanged: function(loadRequest) {
            if (loadRequest.status === WebView.LoadFailedStatus) {
                errorText.text = "The interface could not be loaded.\n" + loadRequest.errorString
                errorBox.visible = true
            } else if (loadRequest.status === WebView.LoadSucceededStatus) {
                errorBox.visible = false
            }
        }
    }

    Rectangle {
        id: errorBox
        visible: false
        anchors.fill: parent
        color: "#0b1220"
        Text {
            id: errorText
            anchors.centerIn: parent
            width: parent.width - 48
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: "white"
            font.pixelSize: 16
        }
    }

    // Android Back button: let the web page close dialogs / go to the previous screen first,
    // and leave the app only when the page has nothing left to go back to.
    Item {
        anchors.fill: parent
        focus: true
        Keys.onBackPressed: function(event) {
            event.accepted = true
            web.runJavaScript("window.appBack ? window.appBack() : false", function(handled) {
                if (handled !== true)
                    Qt.quit()
            })
        }
    }
}
