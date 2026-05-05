#include "../envos.h"

#include <unistd.h>

namespace env
{

bool OsInfo::getCompatibilityMode()
{
  // no-op
  return false;
}

std::optional<bool> OsInfo::getElevated()
{
  return getuid() == 0;
}

}  // namespace env
