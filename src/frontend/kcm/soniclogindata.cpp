/*
 *  SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>
 *  SPDX-FileCopyrightText: 2025 Oliver Beard <olib141@outlook.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFile>
#include <QStandardPaths>

#include "InitSystem.h"
#include "sonicloginsettings.h"

#include "soniclogindata.h"

SonicLoginData::SonicLoginData(QObject *parent)
    : KCModuleData(parent)
    , m_wallpaperSettings(new WallpaperSettings(this))
{
    InitSystem init = detectInitSystem();
    bool relevant = false;

    switch (init) {
    case InitSystem::Systemd: {
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"),
                                                          QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                          QStringLiteral("GetUnitFileState"));
        msg << QStringLiteral("soniclogin.service");

        // it seems system settings wants things to be sync
        // It's not like systemd will ever be down
        QDBusReply<QString> hasSonicLoginReply = QDBusConnection::systemBus().call(msg);
        // a quirk is that if soniclogin is uninstalled systemd replies with an error of invalid args
        relevant = hasSonicLoginReply.value() == QLatin1String("enabled");
        break;
    }
    case InitSystem::OpenRC:
        // Check if soniclogin is in the default runlevel
        // OpenRC uses symlinks in /etc/runlevels/<runlevel>/
        relevant = QFile::exists(QStringLiteral("/etc/runlevels/default/soniclogin"));
        break;
    case InitSystem::BSDInit:
        // Check if soniclogin_enable=YES in /etc/rc.conf or /etc/rc.conf.local
        relevant = QFile::exists(QStringLiteral("/etc/rc.d/soniclogin"))
            && (checkBsdRcConfEnabled(QStringLiteral("/etc/rc.conf")) || checkBsdRcConfEnabled(QStringLiteral("/etc/rc.conf.local")));
        break;
    case InitSystem::Sysvinit:
        // Check if init script exists and is executable
        relevant = QFile::exists(QStringLiteral("/etc/init.d/soniclogin"));
        break;
    case InitSystem::Runit:
        // Check if service is enabled (symlink in /service/ or /etc/service/)
        // runit typically scans /service/ by default
        relevant = QFile::exists(QStringLiteral("/service/soniclogin")) || QFile::exists(QStringLiteral("/etc/service/soniclogin"));
        break;
    case InitSystem::S6:
        // Check if s6 service directory exists
        // s6-svscan scans /service/ or /etc/s6/service/ depending on configuration
        // Also check common Artix-style locations
        relevant = QFile::exists(QStringLiteral("/service/soniclogin-srv")) || QFile::exists(QStringLiteral("/etc/s6/service/soniclogin"))
            || QFile::exists(QStringLiteral("/etc/s6/sv/soniclogin-srv"));
        break;
    case InitSystem::Dinit:
        // Check if dinit service file exists
        relevant = QFile::exists(QStringLiteral("/etc/dinit/soniclogin"));
        break;
    case InitSystem::Unknown:
    default:
        // If we can't detect the init system, check if soniclogin executable exists
        // This is a fallback that assumes if the package is installed, it's relevant
        relevant = QFile::exists(QStandardPaths::findExecutable(QStringLiteral("soniclogin")));
        break;
    }

    setRelevant(relevant);
    m_wallpaperSettings->load();
}

bool SonicLoginData::isDefaults() const
{
    return SonicLoginSettings::getInstance().isDefaults() && m_wallpaperSettings->isDefaults();
}

bool SonicLoginData::checkBsdRcConfEnabled(const QString &path)
{
    QFile rcConf(path);
    if (!rcConf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray contents = rcConf.readAll();
    rcConf.close();
    // Check for soniclogin_enable="YES" or soniclogin_enable="TRUE"
    return contents.contains("soniclogin_enable=\"YES\"") || contents.contains("soniclogin_enable=\"TRUE\"") || contents.contains("soniclogin_enable=YES")
        || contents.contains("soniclogin_enable=TRUE");
}

#include "moc_soniclogindata.cpp"
