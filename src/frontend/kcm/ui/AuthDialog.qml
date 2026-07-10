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

    minimumWidth: Kirigami.Units.gridUnit * 20
    width: Kirigami.Units.gridUnit * 20
    height: contentLayout.implicitHeight + footer.implicitHeight + Kirigami.Units.largeSpacing * 2

    property bool wasSubmitted: false

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                source: "system-lock-screen"
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Kirigami.Units.iconSizes.large * 1.5
                implicitHeight: Kirigami.Units.iconSizes.large * 1.5
            }

            QQC2.Label {
                text: i18n("An application is requesting authentication.")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
        }

        Kirigami.PasswordField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: i18nc("@label:textbox", "Password")
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
        passwordField.text = ""
        show()
        requestActivate()
    }

    function doAccept() {
        dialog.wasSubmitted = true
        kcm.submitAuth(kcm.currentUser, passwordField.text)
        close()
    }

    function doReject() {
        dialog.wasSubmitted = true
        kcm.cancelAuth()
        close()
    }
}
