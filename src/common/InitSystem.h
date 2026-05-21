/*
 * SPDX-FileCopyrightText: 2026 The PLASMALOGIN contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef INIT_SYSTEM_H
#define INIT_SYSTEM_H

#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QTextStream>

// Enum for possible init systems detected at runtime.
enum class InitSystem {
    Unknown,
    Systemd,
    OpenRC,
    Sysvinit,
    Runit,
    S6,
    Dinit,
    BSDInit, // FreeBSD rc.d, OpenBSD rc.d, NetBSD rc.d, etc.
};

// Detect the init system at runtime.
// Uses platform-specific methods for reliable detection.
inline InitSystem detectInitSystem()
{
#ifdef Q_OS_FREEBSD
    qDebug() << "detectInitSystem: Running on FreeBSD, checking for OpenRC indicators";
    // On FreeBSD, PID 1 is always the BSD init.
    // Check for OpenRC by looking for /etc/init.d and openrc-run
    bool hasInitD = QFile::exists(QStringLiteral("/etc/init.d"));
    bool hasOpenrcRun = QFile::exists(QStringLiteral("/usr/local/sbin/openrc-run"));
    qDebug() << "detectInitSystem: FreeBSD - /etc/init.d exists:" << hasInitD << ", /usr/local/sbin/openrc-run exists:" << hasOpenrcRun;
    if (hasInitD && hasOpenrcRun) {
        qDebug() << "detectInitSystem: Detected OpenRC on FreeBSD";
        return InitSystem::OpenRC;
    }
    qDebug() << "detectInitSystem: Detected BSDInit on FreeBSD";
    // Otherwise it's BSD init
    return InitSystem::BSDInit;
#else
    // On Linux, check PID 1's process name via /proc/1/comm
    // This is the most reliable method and works in all standard configurations
    QFile commFile(QStringLiteral("/proc/1/comm"));
    if (commFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString comm = QString::fromLocal8Bit(commFile.readAll()).trimmed();
        commFile.close();
        qDebug() << "detectInitSystem: Read /proc/1/comm:" << comm;

        if (comm == QLatin1String("systemd")) {
            qDebug() << "detectInitSystem: Detected systemd";
            return InitSystem::Systemd;
        } else if (comm == QLatin1String("openrc-init")) {
            qDebug() << "detectInitSystem: Detected openrc-init";
            return InitSystem::OpenRC;
        } else if (comm == QLatin1String("init") || comm == QLatin1String("sysvinit")) {
            // PID 1 is "init" - could be sysvinit, OpenRC, or others.
            qDebug() << "detectInitSystem: PID 1 is 'init', checking for OpenRC indicators";
            // Check for OpenRC-specific runtime indicators.
            bool hasSoftlevel = QFile::exists(QStringLiteral("/run/openrc/softlevel"));
            bool hasHotplugged = QFile::exists(QStringLiteral("/run/openrc/hotplugged"));
            qDebug() << "detectInitSystem: /run/openrc/softlevel exists:" << hasSoftlevel << ", /run/openrc/hotplugged exists:" << hasHotplugged;
            if (hasSoftlevel || hasHotplugged) {
                qDebug() << "detectInitSystem: Detected OpenRC via runtime indicators";
                return InitSystem::OpenRC;
            }
            // Check if openrc-run is available
            QProcess check;
            check.start(QStringLiteral("which"), QStringList() << QStringLiteral("openrc-run"));
            bool openrcRunFound = check.waitForStarted() && check.waitForFinished() && check.exitCode() == 0;
            qDebug() << "detectInitSystem: openrc-run found:" << openrcRunFound;
            if (openrcRunFound) {
                qDebug() << "detectInitSystem: Detected OpenRC via openrc-run path";
                return InitSystem::OpenRC;
            }
            qDebug() << "detectInitSystem: Detected sysvinit";
            return InitSystem::Sysvinit;
        } else if (comm == QLatin1String("runit")) {
            qDebug() << "detectInitSystem: Detected runit";
            return InitSystem::Runit;
        } else if (comm == QLatin1String("s6-svscan")) {
            qDebug() << "detectInitSystem: Detected s6";
            return InitSystem::S6;
        } else if (comm == QLatin1String("dinit")) {
            qDebug() << "detectInitSystem: Detected dinit";
            return InitSystem::Dinit;
        } else {
            qWarning() << "detectInitSystem: Unknown comm value:" << comm;
        }
    } else {
        qWarning() << "detectInitSystem: Could not open /proc/1/comm, using fallback detection";
    }

    // If /proc/1/comm is not available, check for systemd's runtime directory
    // This is the canonical runtime detection method recommended by systemd
    if (QFile::exists(QStringLiteral("/run/systemd/system"))) {
        qDebug() << "detectInitSystem: Detected systemd via /run/systemd/system";
        return InitSystem::Systemd;
    }

    // Check for OpenRC runtime indicators
    bool hasSoftlevel = QFile::exists(QStringLiteral("/run/openrc/softlevel"));
    bool hasHotplugged = QFile::exists(QStringLiteral("/run/openrc/hotplugged"));
    qDebug() << "detectInitSystem: Fallback - /run/openrc/softlevel exists:" << hasSoftlevel << ", /run/openrc/hotplugged exists:" << hasHotplugged;
    if (hasSoftlevel || hasHotplugged) {
        qDebug() << "detectInitSystem: Detected OpenRC via fallback runtime indicators";
        return InitSystem::OpenRC;
    }

    // Fallback: use ps -p 1 -o comm=
    QProcess ps;
    ps.start(QStringLiteral("ps"), QStringList() << QStringLiteral("-p") << QStringLiteral("1") << QStringLiteral("-o") << QStringLiteral("comm="));
    if (ps.waitForStarted() && ps.waitForFinished() && ps.exitCode() == 0) {
        const QString comm = QString::fromLocal8Bit(ps.readAllStandardOutput()).trimmed();
        qDebug() << "detectInitSystem: ps fallback comm:" << comm;

        if (comm == QLatin1String("systemd")) {
            qDebug() << "detectInitSystem: Detected systemd via ps fallback";
            return InitSystem::Systemd;
        } else if (comm == QLatin1String("openrc-init")) {
            qDebug() << "detectInitSystem: Detected openrc-init via ps fallback";
            return InitSystem::OpenRC;
        } else if (comm == QLatin1String("init") || comm == QLatin1String("sysvinit")) {
            // PID 1 is "init" - could be sysvinit, OpenRC, or others.
            qDebug() << "detectInitSystem: ps fallback PID 1 is 'init', checking for OpenRC indicators";
            // Check for OpenRC-specific runtime indicators.
            if (QFile::exists(QStringLiteral("/run/openrc/softlevel")) || QFile::exists(QStringLiteral("/run/openrc/hotplugged"))) {
                qDebug() << "detectInitSystem: Detected OpenRC via ps fallback runtime indicators";
                return InitSystem::OpenRC;
            }
            // Check if openrc-run is available
            QProcess check;
            check.start(QStringLiteral("which"), QStringList() << QStringLiteral("openrc-run"));
            if (check.waitForStarted() && check.waitForFinished() && check.exitCode() == 0) {
                qDebug() << "detectInitSystem: Detected OpenRC via ps fallback openrc-run path";
                return InitSystem::OpenRC;
            }
            qDebug() << "detectInitSystem: Detected sysvinit via ps fallback";
            return InitSystem::Sysvinit;
        } else if (comm == QLatin1String("runit")) {
            qDebug() << "detectInitSystem: Detected runit via ps fallback";
            return InitSystem::Runit;
        } else if (comm == QLatin1String("s6-svscan")) {
            qDebug() << "detectInitSystem: Detected s6 via ps fallback";
            return InitSystem::S6;
        } else if (comm == QLatin1String("dinit")) {
            qDebug() << "detectInitSystem: Detected dinit via ps fallback";
            return InitSystem::Dinit;
        }
    } else {
        qWarning() << "detectInitSystem: ps fallback failed";
    }

    qWarning() << "detectInitSystem: Returning Unknown";
    return InitSystem::Unknown;
#endif
}

// Check if systemd-logind is handling login1, vs elogind
// Both use the same org.freedesktop.login1 bus name, but the service file differs
inline bool isSystemdLogind()
{
    // systemd-logind uses /lib/systemd/systemd-logind
    // elogind uses /lib/elogind/elogind
    QString serviceFile = QStringLiteral("/usr/share/dbus-1/system-services/org.freedesktop.login1.service");

    // Read the service file to determine which backend is in use
    QFile file(serviceFile);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.startsWith(QStringLiteral("Exec="))) {
                QString execPath = line.mid(5).trimmed();
                if (execPath.contains(QStringLiteral("/elogind/"))) {
                    return false; // elogind - use sharevts
                }
                return true; // systemd-logind or other - don't use sharevts
            }
        }
        file.close();
    }

    // Fallback: assume systemd-logind (most common)
    return true;
}

// Get the initial VT based on the detected init system.
// OpenRC uses VT 2 by default (VT 1 is often used by getty/agetty).
// systemd and other init systems use VT 1 by default.
inline int getInitialVt()
{
    InitSystem init = detectInitSystem();
    if (init == InitSystem::OpenRC) {
        return 2;
    }
    return 1;
}

#endif // INIT_SYSTEM_H
