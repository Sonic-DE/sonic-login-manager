/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import org.kde.kirigami as Kirigami
import "../src/frontend/greeter/qml" as Greeter

Rectangle {
    id: root
    width: 480
    height: 240
    color: testBackground
    property color testBackground: "#202428"
    property color testForeground: "#beffe6"
    property alias selector: sessionSelector
    property alias icon: symbolicIcon
    readonly property bool selectorUsesComplementary: sessionSelector.Kirigami.Theme.colorSet === Kirigami.Theme.Complementary
    readonly property color popupForeground: sessionSelector.popup.Kirigami.Theme.textColor
    readonly property color popupBackground: sessionSelector.popup.Kirigami.Theme.backgroundColor

    Greeter.SessionButton {
        id: sessionSelector
        x: 20
        y: 20
        width: 420
        Kirigami.Theme.textColor: root.testForeground
        Kirigami.Theme.backgroundColor: root.testBackground
    }

    Kirigami.Icon {
        id: symbolicIcon
        x: 20
        y: 130
        width: 32
        height: 32
        source: "regression-symbolic"
        Kirigami.Theme.inherit: false
        Kirigami.Theme.textColor: root.testForeground
        color: root.testForeground
    }
}
