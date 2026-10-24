/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
import QtQuick
import org.kde.kirigami as Kirigami
import "../src/frontend/wallpaper" as Wallpaper

Rectangle {
    id: root
    color: "magenta"
    property url testImage
    property bool sourceLayered: true
    property bool blurred: false
    property real blurAmount: 1
    property alias imageStatus: wallpaper.status
    property alias fader: fader
    property color themeBackground: "#202428"
    Kirigami.Theme.backgroundColor: themeBackground

    Image {
        id: wallpaper
        anchors.fill: parent
        source: root.testImage
        smooth: false
        layer.enabled: root.sourceLayered
    }
    Wallpaper.WallpaperFader {
        id: fader
        anchors.fill: parent
        source: wallpaper
        factor: root.blurred ? root.blurAmount : 0
    }
}
