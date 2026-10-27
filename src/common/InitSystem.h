/*
 * SPDX-FileCopyrightText: 2026 The SONICLOGIN contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef INIT_SYSTEM_H
#define INIT_SYSTEM_H

#include <Sonic/InitInfo/InitInfo>

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

// The service manager, as Sonic Init Info reads it from PID 1 and that init's configuration.
inline InitSystem detectInitSystem()
{
    switch (Sonic::InitInfo::serviceManager()) {
    case Sonic::InitInfo::ServiceManager::Systemd:
        return InitSystem::Systemd;
    case Sonic::InitInfo::ServiceManager::OpenRC:
        return InitSystem::OpenRC;
    case Sonic::InitInfo::ServiceManager::SysVinit:
        return InitSystem::Sysvinit;
    case Sonic::InitInfo::ServiceManager::Runit:
        return InitSystem::Runit;
    case Sonic::InitInfo::ServiceManager::S6:
        return InitSystem::S6;
    case Sonic::InitInfo::ServiceManager::Dinit:
        return InitSystem::Dinit;
    case Sonic::InitInfo::ServiceManager::BsdRc:
    case Sonic::InitInfo::ServiceManager::OpenBsdRc:
        return InitSystem::BSDInit;
    case Sonic::InitInfo::ServiceManager::Unknown:
        break;
    }
    return InitSystem::Unknown;
}

// The VT to start on: 2 where getty usually owns VT 1 (OpenRC, runit, s6), else 1.
inline int getInitialVt()
{
    return Sonic::InitInfo::defaultInitialVt(Sonic::InitInfo::serviceManager());
}

#endif // INIT_SYSTEM_H
