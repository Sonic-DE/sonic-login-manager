/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Constants.h"

#include "SelfProvisioner.h"

#include "MessageHandler.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

namespace SONICLOGIN
{

SelfProvisioner::SelfProvisioner()
{
}

SelfProvisioner::~SelfProvisioner() = default;

bool SelfProvisioner::provision()
{
    qDebug() << "SelfProvisioner: Starting system provisioning...";

    // Run provisioning steps in order
    if (!createGreeterUser()) {
        qCritical() << "SelfProvisioner: Failed to create soniclogin user";
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

bool SelfProvisioner::createGreeterUser()
{
    qDebug() << "SelfProvisioner: Checking for soniclogin user...";

    // Check if user exists via getent
    QProcess getent;
    getent.setProgram(QStringLiteral("getent"));
    getent.setArguments({QStringLiteral("passwd"), QStringLiteral("soniclogin")});
    getent.setProcessChannelMode(QProcess::MergedChannels);
    getent.start();
    getent.waitForFinished();

    if (getent.exitCode() == 0) {
        qDebug() << "SelfProvisioner: soniclogin user already exists.";

        // Always ensure home directory points to state dir
        QString output = QString::fromLocal8Bit(getent.readAll());
        QString currentHome = output.section(QLatin1Char(':'), 5, 5);
        if (currentHome != QStringLiteral(STATE_DIR)) {
            qDebug() << "SelfProvisioner: Updating soniclogin home directory to" << QStringLiteral(STATE_DIR);
#ifdef Q_OS_FREEBSD
            // BSD uses different syntax
            runCommandIgnorableFailure(QStringLiteral("pw"),
                                       {QStringLiteral("usermod"), QStringLiteral("soniclogin"), QStringLiteral("-d"), QStringLiteral(STATE_DIR)});
#else
            // Linux uses usermod -d
            runCommandIgnorableFailure(QStringLiteral("usermod"), {QStringLiteral("-d"), QStringLiteral(STATE_DIR), QStringLiteral("soniclogin")});
#endif
        }
    } else {
        qWarning() << "SelfProvisioner: soniclogin user does not exist, skipping user creation";
        qWarning() << "SelfProvisioner: create the soniclogin user manually or run this as root";
    }

    // Add soniclogin user to required groups (video, input, render)
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
        groupsProc.setArguments({QStringLiteral("soniclogin")});
        groupsProc.setProcessChannelMode(QProcess::MergedChannels);
        groupsProc.start();
        groupsProc.waitForFinished();
        QString groupsOutput = QString::fromLocal8Bit(groupsProc.readAll());
        if (groupsOutput.contains(grp)) {
            continue; // Already in group
        }

        qDebug() << "SelfProvisioner: Adding soniclogin to group" << grp;
#ifdef Q_OS_FREEBSD
        // BSD uses pw groupmod
        runCommandIgnorableFailure(QStringLiteral("pw"), {QStringLiteral("groupmod"), grp, QStringLiteral("-m"), QStringLiteral("soniclogin")});
#else
        // Linux uses usermod -aG
        runCommandIgnorableFailure(QStringLiteral("usermod"), {QStringLiteral("-aG"), grp, QStringLiteral("soniclogin")});
#endif
    }

    return true;
}

bool SelfProvisioner::createStateDirectory()
{
    qDebug() << "SelfProvisioner: Setting up state directory:" << QStringLiteral(STATE_DIR);

    const QStringList subdirs = {
        QStringLiteral(STATE_DIR),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.config"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.cache"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.local"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.local/share"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.local/state"),
    };

    for (const QString &path : subdirs) {
        QDir dir;
        if (!dir.exists(path)) {
            if (!dir.mkpath(path)) {
                qCritical() << "SelfProvisioner: Failed to create directory:" << path;
                return false;
            }
            qDebug() << "SelfProvisioner: Created directory:" << path;
        }
    }

    QProcess idProc;
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-u"), QStringLiteral("soniclogin")});
    idProc.setProcessChannelMode(QProcess::MergedChannels);
    idProc.start();
    idProc.waitForFinished();
    bool ok1 = false;
    uid_t uid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok1);

    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-g"), QStringLiteral("soniclogin")});
    idProc.start();
    idProc.waitForFinished();
    bool ok2 = false;
    gid_t gid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok2);

    if (ok1 && ok2) {
        for (const QString &path : subdirs) {
            ::chown(path.toLocal8Bit().constData(), uid, gid);
            ::chmod(path.toLocal8Bit().constData(), 0755);
        }
    }

    return true;
}

bool SelfProvisioner::createRuntimeDirectory()
{
    qDebug() << "SelfProvisioner: Setting up runtime directory:" << QStringLiteral(RUNTIME_DIR);

    QDir dir;
    if (dir.exists(QStringLiteral(RUNTIME_DIR))) {
        qDebug() << "SelfProvisioner: Runtime directory already exists.";
    } else {
        if (!dir.mkpath(QStringLiteral(RUNTIME_DIR))) {
            qCritical() << "SelfProvisioner: Failed to create runtime directory:" << QStringLiteral(RUNTIME_DIR);
            return false;
        }
        qDebug() << "SelfProvisioner: Created runtime directory.";
    }

    // Runtime dir must be owned by the greeter user so it can create its home subdirectories.
    // Keep it world-readable/traversable since this is a system account.
    ::chmod(QStringLiteral(RUNTIME_DIR).toLocal8Bit().constData(), 0755);

    QProcess idProc;
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-u"), QStringLiteral("soniclogin")});
    idProc.setProcessChannelMode(QProcess::MergedChannels);
    idProc.start();
    idProc.waitForFinished();
    bool ok1 = false;
    uid_t uid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok1);

    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-g"), QStringLiteral("soniclogin")});
    idProc.start();
    idProc.waitForFinished();
    bool ok2 = false;
    gid_t gid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok2);

    if (ok1 && ok2) {
        ::chown(QStringLiteral(RUNTIME_DIR).toLocal8Bit().constData(), uid, gid);
    }

    return true;
}

bool SelfProvisioner::setupLogging()
{
    qDebug() << "SelfProvisioner: Setting up logging...";

    // Create log directory and file
    qDebug() << "SelfProvisioner: No journalctl - creating log directory and file.";

    // Create log directory
    QFileInfo logFileInfo(QStringLiteral(LOG_FILE));
    QDir logDirectory = logFileInfo.absoluteDir();
    if (!logDirectory.exists()) {
        if (!logDirectory.mkpath(logDirectory.absolutePath())) {
            qCritical() << "SelfProvisioner: Failed to create log directory:" << logDirectory.absolutePath();
            return false;
        }
    }

    // Rotate log file
    QString oldLog = QStringLiteral(LOG_FILE) + QStringLiteral(".last");
    if (QFile::exists(oldLog)) {
        QFile::remove(oldLog);
    }
    if (QFile::exists(QStringLiteral(LOG_FILE))) {
        QFile::rename(QStringLiteral(LOG_FILE), oldLog);
    }

    // Set directory permissions (777 so soniclogin helper can write)
    ::chmod(logDirectory.absolutePath().toUtf8().constData(), 0777);

    // Create log file
    if (!QFile::exists(QStringLiteral(LOG_FILE))) {
        QFile file(QStringLiteral(LOG_FILE));
        if (!file.open(QIODevice::WriteOnly)) {
            qCritical() << "SelfProvisioner: Failed to create log file:" << QStringLiteral(LOG_FILE);
            return false;
        }
        file.close();
    }

    // Set log file ownership (soniclogin:soniclogin) and permissions (666 so helper can write)
    QProcess idProc;
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-u"), QStringLiteral("soniclogin")});
    idProc.setProcessChannelMode(QProcess::MergedChannels);
    idProc.start();
    idProc.waitForFinished();
    bool ok1 = false;
    uid_t uid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok1);

    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-g"), QStringLiteral("soniclogin")});
    idProc.start();
    idProc.waitForFinished();
    bool ok2 = false;
    gid_t gid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok2);

    if (ok1 && ok2) {
        ::chown(QStringLiteral(LOG_FILE).toLocal8Bit().constData(), uid, gid);
    }
    ::chmod(QStringLiteral(LOG_FILE).toLocal8Bit().constData(), 0666);

    qDebug() << "SelfProvisioner: Log file setup complete.";
    return true;
}

bool SelfProvisioner::cleanupAuthSockets()
{
    qDebug() << "SelfProvisioner: Cleaning up stale auth sockets...";

    // Clean up old auth sockets in /tmp
    const QStringList patterns = {QStringLiteral("/tmp/soniclogin-auth*"), QStringLiteral("/tmp/xauth_*")};

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

} // namespace SONICLOGIN
