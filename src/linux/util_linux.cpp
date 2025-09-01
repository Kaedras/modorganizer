#include "linux/stub.h"
#include "shared/util.h"

#include <overlayfs/overlayfsmanager.h>
#include <sys/prctl.h>
#include <versioninfo.h>

using namespace MOBase;

namespace MOShared
{

Version createVersionInfo()
{
  STUB();
  return {3, 0, 0, VersionInfo::RELEASE_ALPHA};
}

QString getUsvfsVersionString()
{
  return OverlayFsManager::ofsVersionString();
}

void SetThisThreadName(const QString& s)
{
  if (prctl(PR_SET_NAME, s.toStdString().c_str()) == -1) {
    const int e = errno;
    log::error("Error setting thread name, {}", strerror(e));
  }
}

}  // namespace MOShared
