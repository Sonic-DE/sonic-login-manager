/*
    SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami

import org.kde.sonic.login as SonicLogin

QQC2.ComboBox {
    id: root

    // Use the same color-aware style for the surface, label and popup instead
    // of mixing a Plasma SVG surface with a complementary-colored label.
    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
    Kirigami.Theme.inherit: false

    model: SonicLogin.SessionModel
    textRole: "display"

    visible: count > 1
    flat: true
    displayText: i18nd("sonic_login", "Desktop Session: %1", currentText)

    contentItem: QQC2.Label {
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
