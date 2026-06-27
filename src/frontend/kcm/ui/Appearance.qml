/*
SPDX-FileCopyrightText: 2020 David Redondo <kde@david-redondo.de>
SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>

SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: appearanceRoot
    property alias parentLayout: parentLayout
    property var wallpaper: kcm.wallpaperIntegration
    property var configDialog: kcm

    title: i18nc("@title", "Appearance")

    padding: 6

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            showCloseButton: true
        }

        Kirigami.InlineMessage {
            id: successMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Positive
            text: i18nc("@info:status", "Settings applied successfully.")
            showCloseButton: true
        }

        Kirigami.FormLayout {
            id: parentLayout

            QQC2.CheckBox {
                Kirigami.FormData.label: i18n("Clock")
                text: i18n("Show clock")

                checked: kcm.settings.showClock
                onToggled: kcm.settings.showClock = checked

                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "ShowClock"
                }
            }

            Item {
                Kirigami.FormData.isSection: true
            }

            QQC2.ComboBox {
                Kirigami.FormData.label: i18n("Wallpaper type:")
                model: kcm.availableWallpaperPlugins()
                textRole: "name"
                valueRole: "id"
                currentIndex: model.findIndex(wallpaper => wallpaper["id"] === kcm.settings.wallpaperPluginId)
                displayText: model[currentIndex]["name"]

                onActivated: {
                    kcm.settings.wallpaperPluginId = model[index]["id"]
                }

                KCM.SettingStateBinding {
                    configObject: kcm.settings
                    settingName: "WallpaperPluginId"
                }
            }
        }

        WallpaperConfig {
            sourceFile: kcm.wallpaperConfigFile
            onConfigurationChanged: kcm.updateState()
            onConfigurationForceChanged: kcm.updateState()
            Layout.margins: -appearanceRoot.padding
        }
    }

    Connections {
        target: kcm

        function onSyncAttempted() {
            errorMessage.visible = false
            successMessage.visible = true
            successHideTimer.restart()
        }

        function onErrorOccurred(untranslatedMessage) {
            errorMessage.text = i18n(untranslatedMessage)
            errorMessage.visible = untranslatedMessage.length > 0
            successMessage.visible = false
            errorHideTimer.restart()
        }
    }

    Timer {
        id: successHideTimer
        interval: 5000
        onTriggered: successMessage.visible = false
    }

    Timer {
        id: errorHideTimer
        interval: 8000
        onTriggered: errorMessage.visible = false
    }
}
