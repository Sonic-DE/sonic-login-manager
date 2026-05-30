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
#include "plasmaloginsettings.h"

#include "plasmalogindata.h"

PlasmaLoginData::PlasmaLoginData(QObject *parent)
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
        msg << QStringLiteral("plasmalogin.service");

        // it seems system settings wants things to be sync
        // It's not like systemd will ever be down
        QDBusReply<QString> hasPlasmaLoginReply = QDBusConnection::systemBus().call(msg);
        // a quirk is that if plasmalogin is uninstalled systemd replies with an error of invalid args
        relevant = hasPlasmaLoginReply.value() == QLatin1String("enabled");
        break;
    }
    case InitSystem::OpenRC:
        // Check if plasmalogin is in the default runlevel
        // OpenRC uses symlinks in /etc/runlevels/<runlevel>/
        relevant = QFile::exists(QStringLiteral("/etc/runlevels/default/plasmalogin"));
        break;
    case InitSystem::BSDInit:
        // Check if plasmalogin_enable=YES in /etc/rc.conf or /etc/rc.conf.local
        relevant = QFile::exists(QStringLiteral("/etc/rc.d/plasmalogin"))
            && (checkBsdRcConfEnabled(QStringLiteral("/etc/rc.conf")) || checkBsdRcConfEnabled(QStringLiteral("/etc/rc.conf.local")));
        break;
    case InitSystem::Sysvinit:
        // Check if init script exists and is executable
        relevant = QFile::exists(QStringLiteral("/etc/init.d/plasmalogin"));
        break;
    case InitSystem::Runit:
        // Check if service is enabled (symlink in /service/ or /etc/service/)
        // runit typically scans /service/ by default
        relevant = QFile::exists(QStringLiteral("/service/plasmalogin")) || QFile::exists(QStringLiteral("/etc/service/plasmalogin"));
        break;
    case InitSystem::S6:
        // Check if s6 service directory exists
        // s6-svscan scans /service/ or /etc/s6/service/ depending on configuration
        // Also check common Artix-style locations
        relevant = QFile::exists(QStringLiteral("/service/plasmalogin-srv")) || QFile::exists(QStringLiteral("/etc/s6/service/plasmalogin"))
            || QFile::exists(QStringLiteral("/etc/s6/sv/plasmalogin-srv"));
        break;
    case InitSystem::Dinit:
        // Check if dinit service file exists
        relevant = QFile::exists(QStringLiteral("/etc/dinit/plasmalogin"));
        break;
    case InitSystem::Unknown:
    default:
        // If we can't detect the init system, check if plasmalogin executable exists
        // This is a fallback that assumes if the package is installed, it's relevant
        relevant = QFile::exists(QStandardPaths::findExecutable(QStringLiteral("plasmalogin")));
        break;
    }

    setRelevant(relevant);
    m_wallpaperSettings->load();
}

bool PlasmaLoginData::isDefaults() const
{
    return PlasmaLoginSettings::getInstance().isDefaults() && m_wallpaperSettings->isDefaults();
}

bool PlasmaLoginData::checkBsdRcConfEnabled(const QString &path)
{
    QFile rcConf(path);
    if (!rcConf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray contents = rcConf.readAll();
    rcConf.close();
    // Check for plasmalogin_enable="YES" or plasmalogin_enable="TRUE"
    return contents.contains("plasmalogin_enable=\"YES\"") || contents.contains("plasmalogin_enable=\"TRUE\"") || contents.contains("plasmalogin_enable=YES")
        || contents.contains("plasmalogin_enable=TRUE");
}

#include "moc_plasmalogindata.cpp"
