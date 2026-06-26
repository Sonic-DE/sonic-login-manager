/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Constants.h"
#include "config.h"

#include "SelfProvisioner.h"

#include "MessageHandler.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(SONICLOGIN_SELFPROV, "soniclogin.selfprovisioner")

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

    if (!createDbusDirectory()) {
        qCritical() << "SelfProvisioner: Failed to create dbus directory";
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

    importLegacyPlasmaLoginConfigs();

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

    if (!proc.waitForStarted()) {
        qCritical() << "SelfProvisioner: Failed to start command:" << program << args << "error:" << proc.errorString();
        return false;
    }

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

    if (!proc.waitForStarted()) {
        qWarning() << "SelfProvisioner: Failed to start command (ignored):" << program << args << "error:" << proc.errorString();
        return false;
    }

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

bool SelfProvisioner::userExists(const QString &user, QString *passwdLine)
{
    qDebug() << "SelfProvisioner: Checking if user exists:" << user;

#ifdef Q_OS_FREEBSD
    // On FreeBSD, prefer pw usershow as it's more reliable
    QProcess proc;
    proc.setProgram(QStringLiteral("pw"));
    proc.setArguments({QStringLiteral("usershow"), user});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    proc.waitForFinished();

    if (proc.exitCode() == 0) {
        if (passwdLine) {
            *passwdLine = QString::fromLocal8Bit(proc.readAll()).trimmed();
        }
        return true;
    }
#else
    // On Linux, use getent passwd
    QProcess proc;
    proc.setProgram(QStringLiteral("getent"));
    proc.setArguments({QStringLiteral("passwd"), user});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    proc.waitForFinished();

    if (proc.exitCode() == 0) {
        if (passwdLine) {
            *passwdLine = QString::fromLocal8Bit(proc.readAll()).trimmed();
        }
        return true;
    }
#endif

    return false;
}

bool SelfProvisioner::groupExists(const QString &group)
{
    qDebug() << "SelfProvisioner: Checking if group exists:" << group;

#ifdef Q_OS_FREEBSD
    // On FreeBSD, prefer pw groupshow as it's more reliable
    QProcess proc;
    proc.setProgram(QStringLiteral("pw"));
    proc.setArguments({QStringLiteral("groupshow"), group});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    proc.waitForFinished();

    return proc.exitCode() == 0;
#else
    // On Linux, use getent group
    QProcess proc;
    proc.setProgram(QStringLiteral("getent"));
    proc.setArguments({QStringLiteral("group"), group});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    proc.waitForFinished();

    return proc.exitCode() == 0;
#endif
}

QString SelfProvisioner::nologinShell() const
{
    // Check for /usr/sbin/nologin first (preferred)
    if (QFile::exists(QStringLiteral("/usr/sbin/nologin"))) {
        return QStringLiteral("/usr/sbin/nologin");
    }
    // Fall back to /sbin/nologin
    if (QFile::exists(QStringLiteral("/sbin/nologin"))) {
        return QStringLiteral("/sbin/nologin");
    }
    // Last resort /bin/false
    return QStringLiteral("/bin/false");
}

bool SelfProvisioner::getUserIds(uid_t *uid, gid_t *gid)
{
    // Get UID for soniclogin user
    QProcess idProc;
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-u"), QStringLiteral("soniclogin")});
    idProc.setProcessChannelMode(QProcess::MergedChannels);
    idProc.start();
    idProc.waitForFinished();
    bool ok1 = false;
    *uid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok1);

    // Get GID for soniclogin group
    idProc.setProgram(QStringLiteral("id"));
    idProc.setArguments({QStringLiteral("-g"), QStringLiteral("soniclogin")});
    idProc.start();
    idProc.waitForFinished();
    bool ok2 = false;
    *gid = QString::fromLocal8Bit(idProc.readAll()).trimmed().toUInt(&ok2);

    return ok1 && ok2;
}

bool SelfProvisioner::setOwnership(const QString &path, uid_t uid, gid_t gid)
{
    if (::chown(path.toLocal8Bit().constData(), uid, gid) != 0) {
        qCritical() << "SelfProvisioner: Failed to set ownership for" << path << ":" << strerror(errno);
        return false;
    }
    return true;
}

bool SelfProvisioner::setPermissions(const QString &path, mode_t mode)
{
    if (::chmod(path.toLocal8Bit().constData(), mode) != 0) {
        qCritical() << "SelfProvisioner: Failed to set permissions for" << path << ":" << strerror(errno);
        return false;
    }
    return true;
}

bool SelfProvisioner::createGreeterUser()
{
    qDebug() << "SelfProvisioner: Checking for soniclogin user...";

    // Check if user exists
    QString passwdLine;
    if (userExists(QStringLiteral("soniclogin"), &passwdLine)) {
        qDebug() << "SelfProvisioner: soniclogin user already exists.";

        // Always ensure home directory points to state dir
        QString currentHome = passwdLine.section(QLatin1Char(':'), 5, 5);
        if (currentHome != QStringLiteral(STATE_DIR)) {
            qDebug() << "SelfProvisioner: Updating soniclogin home directory to" << QStringLiteral(STATE_DIR);
#ifdef Q_OS_FREEBSD
            // BSD uses different syntax
            if (!runCommand(QStringLiteral("pw"), {QStringLiteral("usermod"), QStringLiteral("soniclogin"), QStringLiteral("-d"), QStringLiteral(STATE_DIR)})) {
                qCritical() << "SelfProvisioner: Failed to update soniclogin home directory";
                return false;
            }
#else
            // Linux uses usermod -d
            if (!runCommand(QStringLiteral("usermod"), {QStringLiteral("-d"), QStringLiteral(STATE_DIR), QStringLiteral("soniclogin")})) {
                qCritical() << "SelfProvisioner: Failed to update soniclogin home directory";
                return false;
            }
#endif
        }
    } else {
        // User doesn't exist, check if we're running as root
        if (geteuid() != 0) {
            qCritical() << "SelfProvisioner: soniclogin user does not exist and we're not running as root. Cannot create user.";
            return false;
        }

        // First ensure the group exists
        if (!groupExists(QStringLiteral("soniclogin"))) {
            qDebug() << "SelfProvisioner: Creating soniclogin group...";
#ifdef Q_OS_FREEBSD
            // BSD uses pw groupadd
            if (!runCommand(QStringLiteral("pw"), {QStringLiteral("groupadd"), QStringLiteral("soniclogin")})) {
                qCritical() << "SelfProvisioner: Failed to create soniclogin group";
                return false;
            }
#else
            // Linux uses groupadd --system
            if (!runCommand(QStringLiteral("groupadd"), {QStringLiteral("--system"), QStringLiteral("soniclogin")})) {
                qCritical() << "SelfProvisioner: Failed to create soniclogin group";
                return false;
            }
#endif
        }

        // Now create the user
        qDebug() << "SelfProvisioner: Creating soniclogin user...";
        QString shell = nologinShell();
#ifdef Q_OS_FREEBSD
        // BSD uses pw useradd
        if (!runCommand(QStringLiteral("pw"),
                        {QStringLiteral("useradd"),
                         QStringLiteral("soniclogin"),
                         QStringLiteral("-g"),
                         QStringLiteral("soniclogin"),
                         QStringLiteral("-d"),
                         QStringLiteral(STATE_DIR),
                         QStringLiteral("-s"),
                         shell})) {
            qCritical() << "SelfProvisioner: Failed to create soniclogin user";
            return false;
        }
#else
        // Linux uses useradd --system
        if (!runCommand(QStringLiteral("useradd"),
                        {QStringLiteral("--system"),
                         QStringLiteral("--gid"),
                         QStringLiteral("soniclogin"),
                         QStringLiteral("--home-dir"),
                         QStringLiteral(STATE_DIR),
                         QStringLiteral("--shell"),
                         shell,
                         QStringLiteral("--no-create-home"),
                         QStringLiteral("soniclogin")})) {
            qCritical() << "SelfProvisioner: Failed to create soniclogin user";
            return false;
        }
#endif

        // Verify the user was created
        if (!userExists(QStringLiteral("soniclogin"))) {
            qCritical() << "SelfProvisioner: User creation command succeeded but user still doesn't exist";
            return false;
        }

        qDebug() << "SelfProvisioner: Successfully created soniclogin user";
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
        if (!groupExists(grp)) {
            continue; // Group doesn't exist, skip
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
        QStringLiteral(STATE_DIR) + QStringLiteral("/.config/fontconfig"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.cache"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.local"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.local/share"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/.local/state"),
        QStringLiteral(STATE_DIR) + QStringLiteral("/wallpapers"),
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

    // Set ownership and permissions for state directories
    uid_t uid;
    gid_t gid;
    if (!getUserIds(&uid, &gid)) {
        qCritical() << "SelfProvisioner: Failed to get soniclogin user/group IDs";
        return false;
    }

    for (const QString &path : subdirs) {
        if (!setOwnership(path, uid, gid)) {
            qCritical() << "SelfProvisioner: Failed to set ownership for directory:" << path;
            return false;
        }
        if (!setPermissions(path, 0755)) {
            qCritical() << "SelfProvisioner: Failed to set permissions for directory:" << path;
            return false;
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
    if (!setPermissions(QStringLiteral(RUNTIME_DIR), 0755)) {
        qCritical() << "SelfProvisioner: Failed to set permissions for runtime directory";
        return false;
    }

    // Set ownership for runtime directory
    uid_t uid;
    gid_t gid;
    if (!getUserIds(&uid, &gid)) {
        qCritical() << "SelfProvisioner: Failed to get soniclogin user/group IDs";
        return false;
    }

    if (!setOwnership(QStringLiteral(RUNTIME_DIR), uid, gid)) {
        qCritical() << "SelfProvisioner: Failed to set ownership for runtime directory";
        return false;
    }

    return true;
}

bool SelfProvisioner::createDbusDirectory()
{
    qDebug() << "SelfProvisioner: Setting up dbus directory...";

    const QString dbusDir = QStringLiteral(STATE_DIR) + QStringLiteral("/.dbus");
    const QString sessionBusDir = dbusDir + QStringLiteral("/session-bus");

    QDir dir;
    if (!dir.exists(sessionBusDir)) {
        if (!dir.mkpath(sessionBusDir)) {
            qCritical() << "SelfProvisioner: Failed to create dbus session-bus directory:" << sessionBusDir;
            return false;
        }
        qDebug() << "SelfProvisioner: Created dbus session-bus directory:" << sessionBusDir;
    }

    // Set ownership (soniclogin:soniclogin) and permissions (700) for dbus directories.
    // Both .dbus and .dbus/session-bus must be private to the greeter user because the
    // session bus socket lives there and must not be accessible to other users.
    uid_t uid;
    gid_t gid;
    if (!getUserIds(&uid, &gid)) {
        qCritical() << "SelfProvisioner: Failed to get soniclogin user/group IDs";
        return false;
    }

    const QStringList dbusPaths = {dbusDir, sessionBusDir};
    for (const QString &path : dbusPaths) {
        if (!setOwnership(path, uid, gid)) {
            qCritical() << "SelfProvisioner: Failed to set ownership for dbus directory:" << path;
            return false;
        }
        if (!setPermissions(path, 0700)) {
            qCritical() << "SelfProvisioner: Failed to set permissions for dbus directory:" << path;
            return false;
        }
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
    if (!setPermissions(logDirectory.absolutePath(), 0777)) {
        qCritical() << "SelfProvisioner: Failed to set permissions for log directory:" << logDirectory.absolutePath();
        return false;
    }

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
    uid_t uid;
    gid_t gid;
    if (!getUserIds(&uid, &gid)) {
        qCritical() << "SelfProvisioner: Failed to get soniclogin user/group IDs";
        return false;
    }

    if (!setOwnership(QStringLiteral(LOG_FILE), uid, gid)) {
        qCritical() << "SelfProvisioner: Failed to set ownership for log file";
        return false;
    }

    if (!setPermissions(QStringLiteral(LOG_FILE), 0666)) {
        qCritical() << "SelfProvisioner: Failed to set permissions for log file";
        return false;
    }

    qDebug() << "SelfProvisioner: Log file setup complete.";
    return true;
}

bool SelfProvisioner::cleanupAuthSockets()
{
    qDebug() << "SelfProvisioner: Cleaning up stale auth sockets...";

    // Each pattern is an absolute path glob. Split into directory and
    // name filter so QDir can resolve absolute paths correctly.
    const QStringList patterns = {QStringLiteral("/tmp/soniclogin-auth*"), QStringLiteral("/tmp/xauth_*"), QStringLiteral("/run/soniclogin/kcm-ipc-*")};

    for (const QString &pattern : patterns) {
        const int slashIdx = pattern.lastIndexOf(QLatin1Char('/'));
        const QString dirPath = (slashIdx > 0) ? pattern.left(slashIdx) : QStringLiteral(".");
        const QString nameFilter = (slashIdx >= 0) ? pattern.mid(slashIdx + 1) : pattern;
        QDir dir(dirPath);
        const QStringList entries = dir.entryList(QStringList(nameFilter), QDir::Files);
        for (const QString &entry : entries) {
            const QString fullPath = dirPath + QLatin1Char('/') + entry;
            qDebug() << "SelfProvisioner: Removing stale socket:" << fullPath;
            if (!QFile::remove(fullPath)) {
                qWarning() << "SelfProvisioner: Failed to remove:" << fullPath;
            }
        }
    }

    return true;
}

bool SelfProvisioner::copyDirRecursive(const QString &srcDir, const QString &dstDir)
{
    if (!QDir().mkpath(dstDir)) {
        qCWarning(SONICLOGIN_SELFPROV) << "copyDirRecursive: failed to create destination" << dstDir;
        return false;
    }

    QDir src(srcDir);
    const QFileInfoList entries = src.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        const QString dstPath = dstDir + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirRecursive(entry.absoluteFilePath(), dstPath)) {
                return false;
            }
        } else if (entry.isFile()) {
            if (!QFile::copy(entry.absoluteFilePath(), dstPath)) {
                qCWarning(SONICLOGIN_SELFPROV) << "copyDirRecursive: failed to copy" << entry.absoluteFilePath() << "->" << dstPath;
                return false;
            }
            QFile::setPermissions(dstPath, QFile::permissions(entry.absoluteFilePath()));
        }
    }
    return true;
}

bool SelfProvisioner::importLegacyPlasmaLoginConfigs()
{
    QDir runtimeDir(RUNTIME_DIR);
    if (!runtimeDir.exists()) {
        QDir().mkpath(RUNTIME_DIR);
    }
    const QString sentinelPath = QStringLiteral(RUNTIME_DIR "/imported-plasmalogin-to-soniclogin");
    if (QFile::exists(sentinelPath)) {
        qCDebug(SONICLOGIN_SELFPROV) << "import: sentinel exists; skipping scan this boot";
        return true;
    }

    struct ImportPair {
        QString legacy;
        QString target;
    };
    QList<ImportPair> pairs;
    pairs.append({QStringLiteral("/etc/plasmalogin.conf"), QStringLiteral(CONFIG_FILE)});
    pairs.append({QStringLiteral("/etc/plasmalogin.conf.d"), QStringLiteral(CONFIG_DIR)});

    int copied = 0;
    int skipped = 0;
    int failed = 0;

    for (const ImportPair &pair : pairs) {
        if (QFileInfo::exists(pair.target)) {
            qCInfo(SONICLOGIN_SELFPROV) << "import: skipping" << pair.legacy << "because" << pair.target << "already exists";
            ++skipped;
            continue;
        }
        if (!QFileInfo::exists(pair.legacy)) {
            qCDebug(SONICLOGIN_SELFPROV) << "import: no source" << pair.legacy << ", nothing to do";
            continue;
        }
        const QFileInfo legacyInfo(pair.legacy);
        bool ok = false;
        if (legacyInfo.isDir()) {
            ok = copyDirRecursive(pair.legacy, pair.target);
        } else if (legacyInfo.isFile()) {
            ok = QFile::copy(pair.legacy, pair.target);
            if (ok) {
                QFile::setPermissions(pair.target, QFile::permissions(pair.legacy));
            }
        } else {
            qCWarning(SONICLOGIN_SELFPROV) << "import:" << pair.legacy << "is neither file nor directory, skipping";
            continue;
        }
        if (ok) {
            qCInfo(SONICLOGIN_SELFPROV) << "import: copied" << pair.legacy << "->" << pair.target;
            ++copied;
        } else {
            qCWarning(SONICLOGIN_SELFPROV) << "import: failed to copy" << pair.legacy << "->" << pair.target;
            ++failed;
        }
    }

    const QString sysDefaultsRoot = QFileInfo(QStringLiteral(SYSTEM_CONFIG_DIR)).absolutePath();
    if (QFileInfo(sysDefaultsRoot).isDir()) {
        qCInfo(SONICLOGIN_SELFPROV) << "import: skipping /usr/lib/plasmalogin because" << sysDefaultsRoot << "already exists";
        ++skipped;
    } else if (!QFileInfo(QStringLiteral("/usr/lib/plasmalogin")).isDir()) {
        qCDebug(SONICLOGIN_SELFPROV) << "import: no source /usr/lib/plasmalogin, nothing to do";
    } else if (copyDirRecursive(QStringLiteral("/usr/lib/plasmalogin"), sysDefaultsRoot)) {
        qCInfo(SONICLOGIN_SELFPROV) << "import: copied /usr/lib/plasmalogin to" << sysDefaultsRoot;
        ++copied;
    } else {
        qCWarning(SONICLOGIN_SELFPROV) << "import: failed to copy /usr/lib/plasmalogin to" << sysDefaultsRoot;
        ++failed;
    }

    QFile sentinel(sentinelPath);
    if (sentinel.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sentinel.write("ok\n");
        sentinel.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        sentinel.close();
    } else {
        qCWarning(SONICLOGIN_SELFPROV) << "import: failed to create sentinel" << sentinelPath;
    }

    qCInfo(SONICLOGIN_SELFPROV) << "import: copied" << copied << "files, skipped" << skipped << "existing targets," << failed << "failed";
    return failed == 0;
}

} // namespace SONICLOGIN
