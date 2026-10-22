// Exercise direct startup independently of the test host's PID 1. The systemd
// oneshot uses --apply-kscreen and exits before init-system detection.
#pragma once
#define detectInitSystem detectHostInitSystem
#include "../../src/common/InitSystem.h"
#undef detectInitSystem

inline InitSystem detectInitSystem()
{
    return InitSystem::OpenRC;
}
