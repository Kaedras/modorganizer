#include "env.h"
#include "envmodule.h"
#include "settings.h"

#include <QStandardPaths>
#include <utility.h>

using namespace Qt::StringLiterals;

namespace sanity
{

using namespace MOBase;

int checkBlocked()
{
  // no-op
  return 0;
}

int checkIncompatibleModule(const env::Module&)
{
  // no-op
  return 0;
}

std::vector<std::pair<QString, QString>> getSystemDirectories()
{
  // folder ids and display names for logging
  const std::vector<std::pair<QStandardPaths::StandardLocation, QString>>
      systemFolderIDs = {{QStandardPaths::DesktopLocation, u"on the desktop"_s},
                         {QStandardPaths::DocumentsLocation, u"in Documents"_s},
                         {QStandardPaths::DownloadLocation, u"in Downloads"_s}};

  std::vector<std::pair<QString, QString>> systemDirs;

  for (auto&& p : systemFolderIDs) {
    const auto dir = MOBase::getOptionalKnownFolder(p.first);

    if (!dir.isEmpty()) {
      auto path = QDir::toNativeSeparators(dir).toLower();
      if (!path.endsWith('/')) {
        path.append("/"_L1);
      }

      systemDirs.push_back({path, p.second});
    }
  }

  return systemDirs;
}

}  // namespace sanity
