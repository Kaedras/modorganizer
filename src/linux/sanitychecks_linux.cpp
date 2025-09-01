#include "envmodule.h"
#include "settings.h"
#include <QStandardPaths>
#include <utility.h>

namespace sanity
{

using namespace MOBase;

std::vector<std::pair<QString, QString>> getSystemDirectories()
{
  // folder ids and display names for logging
  const std::vector<std::pair<QStandardPaths::StandardLocation, QString>>
      systemFolderIDs = {{QStandardPaths::DesktopLocation, "on the desktop"},
                         {QStandardPaths::DocumentsLocation, "in Documents"},
                         {QStandardPaths::DownloadLocation, "in Downloads"}};

  std::vector<std::pair<QString, QString>> systemDirs;

  for (auto&& p : systemFolderIDs) {
    const auto dir = MOBase::getOptionalKnownFolder(p.first);

    if (!dir.isEmpty()) {
      auto path = QDir::toNativeSeparators(dir).toLower();
      if (!path.endsWith("/")) {
        path += "/";
      }

      systemDirs.push_back({path, p.second});
    }
  }

  return systemDirs;
}

}  // namespace sanity
