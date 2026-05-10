#include "shared/util.h"
#include "version.h"
#include <QDir>
#include <sys/prctl.h>
#include <usvfs-fuse/usvfsmanager.h>

using namespace MOBase;

namespace MOShared
{

Version createVersionInfo()
{
  return Version::parse(ORGANIZER_VERSION);
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
  for (QString& entry :
       dir.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
    if (entry.compare(fileName, Qt::CaseInsensitive) == 0) {
      return std::move(entry);
    }
  }
  return fileName;
}

}  // namespace MOShared
