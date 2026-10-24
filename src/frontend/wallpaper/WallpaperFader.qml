/*
    SPDX-FileCopyrightText: 2014 Aleix Pol Gonzalez <aleixpol@blue-systems.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Effects

import org.kde.kirigami as Kirigami

Item {
    id: wallpaperFader

    property alias source: wallpaperBlur.source
    property real factor: 0

    Behavior on factor {
        NumberAnimation {
            target: wallpaperFader
            property: "factor"
            duration: Kirigami.Units.veryLongDuration * 2
            easing.type: Easing.InOutQuad
        }
    }

    MultiEffect {
        id: wallpaperBlur
        anchors.fill: parent
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: 50
        blur: wallpaperFader.factor
        blurMultiplier: 0
    }

    ShaderEffect {
        id: wallpaperShader
        anchors.fill: parent

        supportsAtlasTextures: true

        property var source: ShaderEffectSource {
            sourceItem: wallpaperBlur
            live: true
            hideSource: true
            textureMirroring: ShaderEffectSource.NoMirroring
        }

        // Blend the actual Window background into the blurred wallpaper,
        // preserving the scheme's hue as well as its light/dark appearance.
        readonly property color tintColor: Kirigami.Theme.backgroundColor
        readonly property real tintAmount: 0.35 * wallpaperFader.factor

        fragmentShader: "qrc:/qt/qml/org/sonic/login/wallpaper/shaders/WallpaperFader.frag.qsb"
    }
}
