#pragma once

#ifdef __unix__
#include "linux/envmodule.h"
#else
#include "win32/envmodule.h"
#endif
