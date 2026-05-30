/*
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include "config.h"

#include "sonicloginsettingsdefaults.h"

QString SonicLoginSettingsDefaults::s_defaultUser;
QString SonicLoginSettingsDefaults::s_defaultSession;
bool SonicLoginSettingsDefaults::s_defaultRelogin;
QString SonicLoginSettingsDefaults::s_defaultPreselectedUser;
QString SonicLoginSettingsDefaults::s_defaultPreselectedSession;
bool SonicLoginSettingsDefaults::s_defaultShowClock;
QString SonicLoginSettingsDefaults::s_defaultWallpaperPluginId;

SonicLoginSettingsDefaults::SonicLoginSettingsDefaults(KSharedConfigPtr config, QObject *parent)
    : KConfigSkeleton(config, parent)
{
    auto defaultConfig = KSharedConfig::openConfig(QStringLiteral(SONICLOGIN_SYSTEM_CONFIG_FILE), KConfig::CascadeConfig);
    s_defaultUser = defaultConfig->group(QStringLiteral("AutoLogin")).readEntry("User", "");
    s_defaultSession = defaultConfig->group(QStringLiteral("AutoLogin")).readEntry("Session", "");
    s_defaultRelogin = defaultConfig->group(QStringLiteral("AutoLogin")).readEntry("Relogin", false);
    s_defaultPreselectedUser = defaultConfig->group(QStringLiteral("Greeter")).readEntry("PreselectedUser", "");
    s_defaultPreselectedSession = defaultConfig->group(QStringLiteral("Greeter")).readEntry("PreselectedSession", "");
    s_defaultShowClock = defaultConfig->group(QStringLiteral("Greeter")).readEntry("ShowClock", true);
    s_defaultWallpaperPluginId = defaultConfig->group(QStringLiteral("Greeter")).readEntry("WallpaperPluginId", "org.kde.image");
}

QString SonicLoginSettingsDefaults::defaultUser()
{
    return s_defaultUser;
}

QString SonicLoginSettingsDefaults::defaultSession()
{
    return s_defaultSession;
}

bool SonicLoginSettingsDefaults::defaultRelogin()
{
    return s_defaultRelogin;
}

QString SonicLoginSettingsDefaults::defaultPreselectedUser()
{
    return s_defaultPreselectedUser;
}

QString SonicLoginSettingsDefaults::defaultPreselectedSession()
{
    return s_defaultPreselectedSession;
}

bool SonicLoginSettingsDefaults::defaultShowClock()
{
    return s_defaultShowClock;
}

QString SonicLoginSettingsDefaults::defaultWallpaperPluginId()
{
    return s_defaultWallpaperPluginId;
}

#include "moc_sonicloginsettingsdefaults.cpp"
