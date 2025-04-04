#include "linux/stub.h"
#include "shared/util.h"
#include <overlayfs/overlayfsmanager.h>
#include <sys/prctl.h>

namespace MOShared
{

MOBase::Version createVersionInfo()
{
  STUB();
  return {3, 0, 0};
}

QString getUsvfsVersionString()
{
  return OverlayFsManager::ofsVersionString();
}

void SetThisThreadName(const QString& s)
{
  if (prctl(PR_SET_NAME, s.toStdString().c_str()) == -1) {
    const int e = errno;
    MOBase::log::error("Error setting thread name, {}", strerror(e));
  }
}

}  // namespace MOShared
