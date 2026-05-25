/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "SelfProvisioner.h"

#include "InitSystem.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PLASMALOGIN
{

SelfProvisioner::SelfProvisioner()
    : m_hasJournalctl(false)
{
    // State directory - where plasmalogin user data is stored
    m_stateDir = QStringLiteral("/var/lib/plasmalogin");

    // Runtime directory - for transient runtime files
    m_runtimeDir = QStringLiteral("/run/plasmalogin");

    // Log directory and file - only used when journalctl is NOT available
    m_logDir = QStringLiteral("/var/log/sonic");
    m_logFile = QStringLiteral("/var/log/sonic/loginmanager.log");
}

SelfProvisioner::~SelfProvisioner() = default;

bool SelfProvisioner::provision()
{
    qDebug() << "SelfProvisioner: Starting system provisioning...";

    // Detect journalctl availability
    QProcess proc;
    proc.setProgram(QStringLiteral("which"));
    proc.setArguments({QStringLiteral("journalctl")});
    proc.start();
    proc.waitForFinished();
    m_hasJournalctl = (proc.exitCode() == 0);
    qDebug() << "SelfProvisioner: journalctl detected:" << m_hasJournalctl;

    // Run provisioning steps in order
    if (!createPlasmaloginUser()) {
        qCritical() << "SelfProvisioner: Failed to create plasmalogin user";
        return false;
    }

    if (!createStateDirectory()) {
        qCritical() << "SelfProvisioner: Failed to create state directory";
        return false;
    }

    if (!createRuntimeDirectory()) {
        qCritical() << "SelfProvisioner: Failed to create runtime directory";
        return false;
    }

    if (!setupLogging()) {
        qCritical() << "SelfProvisioner: Failed to set up logging";
        return false;
    }

    if (!cleanupAuthSockets()) {
        qCritical() << "SelfProvisioner: Failed to clean up auth sockets";
        return false;
    }

    qDebug() << "SelfProvisioner: System provisioning complete.";
    return true;
}

bool SelfProvisioner::runCommand(const QString &program, const QStringList &args)
{
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForFinished(-1)) {
        qCritical() << "SelfProvisioner: Command timed out:" << program << args;
        return false;
    }
    if (proc.exitCode() != 0) {
        qCritical() << "SelfProvisioner: Command failed:" << program << args << "exit code:" << proc.exitCode() << "output:" << proc.readAll();
        return false;
    }
    return true;
}

bool SelfProvisioner::runCommandIgnorableFailure(const QString &program, const QStringList &args)
{
    QProcess proc;
    proc.setProgram(program);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForFinished(-1)) {
        qWarning() << "SelfProvisioner: Command timed out (ignored):" << program << args;
        return false;
    }
    if (proc.exitCode() != 0) {
        qWarning() << "SelfProvisioner: Command failed (ignored):" << program << args << "exit code:" << proc.exitCode();
        return false;
    }
    return true;
}

bool SelfProvisioner::createPlasmaloginUser()
{
    qDebug() << "SelfProvisioner: Checking for plasmalogin user...";

    // Detect the init system at runtime
    InitSystem initSystem = detectInitSystem();

    // On systemd, user creation is handled by systemd-sysusers via .sysuser.conf
    // Skip user/group creation since systemd-sysusers runs at boot
    if (initSystem == InitSystem::Systemd) {
        qDebug() << "SelfProvisioner: systemd detected - user creation handled by systemd-sysusers";
        return true;
    }

    // Check if user exists via getent
    QProcess getent;
    getent.setProgram(QStringLiteral("getent"));
    getent.setArguments({QStringLiteral("passwd"), QStringLiteral("plasmalogin")});
    getent.setProcessChannelMode(QProcess::MergedChannels);
    getent.start();
    getent.waitForFinished();

    if (getent.exitCode() == 0) {
        qDebug() << "SelfProvisioner: plasmalogin user already exists.";

        // Ensure home directory points to state dir
        QString output = QString::fromLocal8Bit(getent.readAll());
        QString currentHome = output.section(QLatin1Char(':'), 5, 5);
        if (currentHome != m_stateDir) {
            qDebug() << "SelfProvisioner: Updating plasmalogin home directory to" << m_stateDir;
#ifdef Q_OS_FREEBSD
            // BSD uses different syntax
            runCommandIgnorableFailure(QStringLiteral("pw"), {QStringLiteral("usermod"), QStringLiteral("plasmalogin"), QStringLiteral("-d"), m_stateDir});
#else
            // Linux uses usermod -d
            runCommandIgnorableFailure(QStringLiteral("usermod"), {QStringLiteral("-d"), m_stateDir, QStringLiteral("plasmalogin")});
#endif
        }
    } else {
        qDebug() << "SelfProvisioner: Creating plasmalogin user...";

#ifdef Q_OS_FREEBSD
        // BSD uses pw useradd
        if (!runCommandIgnorableFailure(QStringLiteral("pw"),
                                        {QStringLiteral("useradd"),
                                         QStringLiteral("-n"),
                                         QStringLiteral("plasmalogin"),
                                         QStringLiteral("-d"),
                                         m_stateDir,
                                         QStringLiteral("-s"),
                                         QStringLiteral("/usr/sbin/nologin"),
                                         QStringLiteral("-c"),
                                         QStringLiteral("Sonic Login Greeter Account")})) {
            qCritical() << "SelfProvisioner: Failed to create plasmalogin user";
            return false;
        }
#else
        // Linux uses useradd
        if (!runCommandIgnorableFailure(QStringLiteral("useradd"),
                                        {QStringLiteral("-r"),
                                         QStringLiteral("-s"),
                                         QStringLiteral("/sbin/nologin"),
                                         QStringLiteral("-d"),
                                         m_stateDir,
                                         QStringLiteral("-c"),
                                         QStringLiteral("Sonic Login Greeter Account"),
                                         QStringLiteral("plasmalogin")})) {
            qCritical() << "SelfProvisioner: Failed to create plasmalogin user";
            return false;
        }
#endif
    }

    // Add plasmalogin user to required groups (video, input, render)
#ifndef Q_OS_FREEBSD
    const QStringList groups = {QStringLiteral("video"), QStringLiteral("input"), QStringLiteral("render")};
#else
    // On FreeBSD, operator group is needed for console/device access
    const QStringList groups = {QStringLiteral("video"), QStringLiteral("input"), QStringLiteral("render"), QStringLiteral("operator")};
#endif
    for (const QString &grp : groups) {
        // Check if group exists
        QProcess getgrent;
        getgrent.setProgram(QStringLiteral("getent"));
        getgrent.setArguments({QStringLiteral("group"), grp});
        getgrent.setProcessChannelMode(QProcess::MergedChannels);
        getgrent.start();
        getgrent.waitForFinished();
        if (getgrent.exitCode() != 0) {
            continue; // Group doesn't exist, skip
        }

        // Check if user is already in group
        QProcess groupsProc;
        groupsProc.setProgram(QStringLiteral("groups"));
        groupsProc.setArguments({QStringLiteral("plasmalogin")});
        groupsProc.setProcessChannelMode(QProcess::MergedChannels);
        groupsProc.start();
        groupsProc.waitForFinished();
        QString groupsOutput = QString::fromLocal8Bit(groupsProc.readAll());
        if (groupsOutput.contains(grp)) {
            continue; // Already in group
        }

        qDebug() << "SelfProvisioner: Adding plasmalogin to group" << grp;
#ifdef Q_OS_FREEBSD
        // BSD uses pw groupmod
        runCommandIgnorableFailure(QStringLiteral("pw"), {QStringLiteral("groupmod"), grp, QStringLiteral("-m"), QStringLiteral("plasmalogin")});
#else
        // Linux uses usermod -aG
        runCommandIgnorableFailure(QStringLiteral("usermod"), {QStringLiteral("-aG"), grp, QStringLiteral("plasmalogin")});
#endif
    }

    return true;
}

bool SelfProvisioner::createStateDirectory()
{
    qDebug() << "SelfProvisioner: Setting up state directory:" << m_stateDir;

    QDir dir;
    if (dir.exists(m_stateDir)) {
        qDebug() << "SelfProvisioner: State directory already exists.";
    } else {
        if (!dir.mkpath(m_stateDir)) {
            qCritical() << "SelfProvisioner: Failed to create state directory:" << m_stateDir;
            return false;
        }
        qDebug() << "SelfProvisioner: Created state directory.";
    }

    // Set directory permissions (750 - owner rwx, group r-x, others none)
    ::chmod(m_stateDir.toLocal8Bit().constData(), 0750);

    // Set ownership to plasmalogin user/group
    QProcess idProc;
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-u"), QStringLiteral("plasmalogin")});
    idProc.setProcessChannelMode(QProcess::MergedChannels);
    idProc.start();
    idProc.waitForFinished();
    bool ok1 = false;
    uid_t uid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok1);

    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-g"), QStringLiteral("plasmalogin")});
    idProc.start();
    idProc.waitForFinished();
    bool ok2 = false;
    gid_t gid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok2);

    if (ok1 && ok2) {
        ::chown(m_stateDir.toLocal8Bit().constData(), uid, gid);
    }

    return true;
}

bool SelfProvisioner::createRuntimeDirectory()
{
    qDebug() << "SelfProvisioner: Setting up runtime directory:" << m_runtimeDir;

    QDir dir;
    if (dir.exists(m_runtimeDir)) {
        qDebug() << "SelfProvisioner: Runtime directory already exists.";
    } else {
        if (!dir.mkpath(m_runtimeDir)) {
            qCritical() << "SelfProvisioner: Failed to create runtime directory:" << m_runtimeDir;
            return false;
        }
        qDebug() << "SelfProvisioner: Created runtime directory.";
    }

    // Runtime dir is owned by root, 711
    ::chmod(m_runtimeDir.toLocal8Bit().constData(), 0711);
    ::chown(m_runtimeDir.toLocal8Bit().constData(), 0, 0);

    return true;
}

bool SelfProvisioner::setupLogging()
{
    qDebug() << "SelfProvisioner: Setting up logging...";

    if (m_hasJournalctl) {
        qDebug() << "SelfProvisioner: journalctl is available - using systemd logging.";
        return true;
    }

    // No journalctl - create log directory and file
    qDebug() << "SelfProvisioner: No journalctl - creating log directory and file.";

    // Rotate log file
    QString oldLog = m_logFile + QStringLiteral(".last");
    if (QFile::exists(oldLog)) {
        QFile::remove(oldLog);
    }
    if (QFile::exists(m_logFile)) {
        QFile::rename(m_logFile, oldLog);
    }

    // Create log directory
    QDir logDirObj(m_logDir);
    if (!logDirObj.exists()) {
        if (!logDirObj.mkpath(m_logDir)) {
            qCritical() << "SelfProvisioner: Failed to create log directory:" << m_logDir;
            return false;
        }
    }

    // Set directory permissions (777 so plasmalogin helper can write)
    ::chmod(m_logDir.toLocal8Bit().constData(), 0777);

    // Create log file
    if (!QFile::exists(m_logFile)) {
        QFile file(m_logFile);
        if (!file.open(QIODevice::WriteOnly)) {
            qCritical() << "SelfProvisioner: Failed to create log file:" << m_logFile;
            return false;
        }
        file.close();
    }

    // Set log file ownership (plasmalogin:plasmalogin) and permissions (666 so helper can write)
    QProcess idProc;
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-u"), QStringLiteral("plasmalogin")});
    idProc.setProcessChannelMode(QProcess::MergedChannels);
    idProc.start();
    idProc.waitForFinished();
    bool ok1 = false;
    uid_t uid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok1);

    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-g"), QStringLiteral("plasmalogin")});
    idProc.start();
    idProc.waitForFinished();
    bool ok2 = false;
    gid_t gid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok2);

    if (ok1 && ok2) {
        ::chown(m_logFile.toLocal8Bit().constData(), uid, gid);
    }
    ::chmod(m_logFile.toLocal8Bit().constData(), 0666);

    qDebug() << "SelfProvisioner: Log file setup complete.";
    return true;
}

bool SelfProvisioner::cleanupAuthSockets()
{
    qDebug() << "SelfProvisioner: Cleaning up stale auth sockets...";

    // Clean up old auth sockets in /tmp
    const QStringList patterns = {QStringLiteral("/tmp/plasmalogin-auth*"), QStringLiteral("/tmp/xauth_*")};

    for (const QString &pattern : patterns) {
        QDir dir;
        QStringList entries = dir.entryList(QStringList(pattern), QDir::Files);
        for (const QString &entry : entries) {
            QString fullPath = QStringLiteral("/tmp/") + entry;
            qDebug() << "SelfProvisioner: Removing stale socket:" << fullPath;
            if (!QFile::remove(fullPath)) {
                qWarning() << "SelfProvisioner: Failed to remove:" << fullPath;
            }
        }
    }

    return true;
}

} // namespace PLASMALOGIN
