#pragma once

#ifdef __unix__
#include "linux/loot_linux.h"
#else
#include "win32/loot_win32.h"
#endif