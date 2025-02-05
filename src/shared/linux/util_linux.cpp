#include "shared/util.h"
#include "linux/stub.h"

namespace MOShared
{

MOBase::Version createVersionInfo()
{
  STUB();
  return {0,0,0};
}
QString getUsvfsVersionString()
{
  STUB();
  return {};
}

void SetThisThreadName(const QString& s)
{
  STUB();
}

}  // namespace MOShared
