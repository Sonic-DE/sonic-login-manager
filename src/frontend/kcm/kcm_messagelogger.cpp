/*
 * SPDX-FileCopyrightText: 2026 SonicDE Community
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <Sonic/Logging/Logging>

// The KCM runs inside systemsettings or kcmshell6, so it logs to the host's file, unless the host installed its own handler.
SONIC_LOGGING_COMPONENT("org.sonicde.loginmanager.kcm")
SONIC_LOGGING_AUTO_INSTALL()
