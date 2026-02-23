#include "linux/stub.h"
#include "shared/util.h"

#include <sys/prctl.h>
#include <usvfs-fuse/usvfsmanager.h>
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
  return UsvfsManager::usvfsVersionString();
}

void SetThisThreadName(const QString& s)
{
  if (prctl(PR_SET_NAME, s.toStdString().c_str()) == -1) {
    const int e = errno;
    log::error("Error setting thread name, {}", strerror(e));
  }
}

QString findFileNameCaseInsensitive(const QDir& dir, const QString& fileName)
{
  for (const auto& entry :
       dir.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
    if (entry.compare(fileName, Qt::CaseInsensitive) == 0) {
      return entry;
    }
  }
  return fileName;
}

}  // namespace MOShared
