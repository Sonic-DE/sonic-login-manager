/*
SPDX-FileCopyrightText: 2025 SonicDE Contributors
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami

QQC2.ApplicationWindow {
    id: dialog

    title: i18nc("@title:window", "Authentication Required")
    modality: Qt.WindowModal
    flags: Qt.Dialog

    minimumWidth: Kirigami.Units.gridUnit * 25
    width: Kirigami.Units.gridUnit * 25
    height: contentLayout.implicitHeight + footer.implicitHeight + Kirigami.Units.largeSpacing * 2

    // Guards onClosing so it only calls cancelAuth() when the user
    // closes via the title bar X button, not after submit/reject.
    property bool wasSubmitted: false

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            text: i18n("Enter your credentials to apply Sonic Login settings.")
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }

        QQC2.TextField {
            id: usernameField
            Layout.fillWidth: true
            placeholderText: i18nc("@label:textbox", "Username")
            text: kcm.currentUser
            onAccepted: passwordField.forceActiveFocus()
        }

        Kirigami.PasswordField {
            id: passwordField
            Layout.fillWidth: true
            onAccepted: dialog.doAccept()
        }
    }

    footer: QQC2.DialogButtonBox {
        standardButtons: QQC2.DialogButtonBox.Ok | QQC2.DialogButtonBox.Cancel
        onAccepted: dialog.doAccept()
        onRejected: dialog.doReject()
    }

    Shortcut {
        sequences: [StandardKey.Cancel, StandardKey.Close, "Escape"]
        onActivated: dialog.doReject()
    }

    onClosing: (close) => {
        if (!dialog.wasSubmitted) {
            kcm.cancelAuth()
        }
    }

    onVisibleChanged: {
        if (visible) {
            passwordField.forceActiveFocus(Qt.ActiveWindowFocusReason)
        }
    }

    function openAndClear() {
        dialog.wasSubmitted = false
        if (usernameField.text.length === 0) {
            usernameField.text = kcm.currentUser
        }
        passwordField.text = ""
        show()
        requestActivate()
    }

    function doAccept() {
        dialog.wasSubmitted = true
        kcm.submitAuth(usernameField.text, passwordField.text)
        close()
    }

    function doReject() {
        dialog.wasSubmitted = true
        kcm.cancelAuth()
        close()
    }
}