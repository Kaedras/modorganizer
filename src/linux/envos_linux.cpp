#include "../envos.h"

using namespace MOBase;
using namespace Qt::StringLiterals;

namespace env
{

bool OsInfo::compatibilityMode() const
{
  // no-op
  return false;
}

std::optional<bool> OsInfo::isElevated() const
{
  return getuid() == 0;
}

}  // namespace env
