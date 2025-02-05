#pragma once

#ifdef __unix__
#include "linux/loot.h"
#else
#include "win32/loot.h"
#endif