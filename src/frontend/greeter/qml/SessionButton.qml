/*
    SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents3

import org.kde.sonic.login as SonicLogin

PlasmaComponents3.ComboBox {
    id: root

    // Use the selected Plasma SVG assets as well as the configured palette.
    // Match Main.qml's selected scheme, rather than its complementary set.
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    model: SonicLogin.SessionModel
    textRole: "display"

    visible: count > 1
    flat: true
    displayText: i18nd("sonic_login", "Desktop Session: %1", currentText)

    contentItem: PlasmaComponents3.Label {
        font: root.font
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        text: root.displayText
    }

    QQC2.ToolTip.text: currentText
    QQC2.ToolTip.visible: hovered && contentItem.truncated && !popup.visible
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    Connections {
        target: root.popup

        function onVisibleChanged() {
            SonicLogin.GreeterState.setSessionButtonPopupVisible(root, root.popup.visible);
        }
    }

    Component.onCompleted: SonicLogin.GreeterState.setSessionButtonPopupVisible(root, root.popup.visible)
    Component.onDestruction: {
        if (SonicLogin.GreeterState) {
            SonicLogin.GreeterState.setSessionButtonPopupVisible(root, false);
        }
    }

    currentIndex: count > 0 ? SonicLogin.GreeterState.sessionIndex : -1

    signal sessionChanged()

    // Initialization and model updates can also change currentIndex. Only a
    // user's activation should change the shared login session or form focus.
    onActivated: (index) => {
        if (index >= 0 && index < count) {
            SonicLogin.GreeterState.sessionIndex = index;
            sessionChanged();
        }
    }
}
