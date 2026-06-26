/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef SONICLOGIN_SELFPROVISIONER_H
#define SONICLOGIN_SELFPROVISIONER_H

#include <QString>

namespace SONICLOGIN
{

/**
 * @brief Handles first-run self-provisioning for the soniclogin daemon.
 *
 * Performs system setup that would otherwise require an init script:
 * - Creates the soniclogin system user and group
 * - Creates state, runtime, and log directories
 * - Cleans up stale auth sockets
 *
 * Called at the very start of main() before DaemonApp is constructed.
 */
class SelfProvisioner
{
public:
    SelfProvisioner();
    ~SelfProvisioner();

    /**
     * Run all provisioning steps.
     * Returns true on success, false on failure.
     * Logs errors via qCritical().
     */
    bool provision();

private:
    bool runCommand(const QString &program, const QStringList &args);
    bool runCommandIgnorableFailure(const QString &program, const QStringList &args);

    // One-time import of legacy /etc/plasmalogin.* and /usr/lib/plasmalogin/
    // configs into the Sonic Login Manager locations. Copies (does not move),
    // never touches the source files, and only writes to destinations that
    // do not already exist.
    bool importLegacyPlasmaLoginConfigs();

    bool createGreeterUser();
    bool createStateDirectory();
    bool createRuntimeDirectory();
    bool createDbusDirectory();
    bool setupLogging();
    bool cleanupAuthSockets();

    // Helper methods for user/group management
    bool userExists(const QString &user, QString *passwdLine = nullptr);
    bool groupExists(const QString &group);
    QString nologinShell() const;

    // Helper methods for file operations
    bool getUserIds(uid_t *uid, gid_t *gid);
    bool setOwnership(const QString &path, uid_t uid, gid_t gid);
    bool setPermissions(const QString &path, mode_t mode);

    // Recursive directory copy helper used by importLegacyPlasmaLoginConfigs.
    static bool copyDirRecursive(const QString &srcDir, const QString &dstDir);

    QString m_stateDir;
    QString m_runtimeDir;
    QString m_logDir;
    QString m_logFile;
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_SELFPROVISIONER_H
