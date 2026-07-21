/*
    SPDX-FileCopyrightText: 2026 Oliver Beard <olib141@outlook.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2

import org.kde.plasma.components as PlasmaComponents
import org.kde.kirigami as Kirigami

import org.kde.sonic.login as SonicLogin

PlasmaComponents.ComboBox {
    id: root

    model: SonicLogin.SessionModel
    textRole: "display"

    visible: count > 1
    flat: true
    displayText: i18nd("soniclogin", "Desktop Session: %1", currentText)

    contentItem: QQC2.Label {
        font: root.font
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight

        text: root.displayText
    }

    PlasmaComponents.ToolTip.text: currentText
    PlasmaComponents.ToolTip.visible: hovered && contentItem.truncated && !popup.visible
    PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay

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
