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

    bool createGreeterUser();
    bool createStateDirectory();
    bool createRuntimeDirectory();
    bool setupLogging();
    bool cleanupAuthSockets();

    QString m_stateDir;
    QString m_runtimeDir;
    QString m_logDir;
    QString m_logFile;
    bool m_hasJournalctl;
};

} // namespace SONICLOGIN

#endif // SONICLOGIN_SELFPROVISIONER_H
