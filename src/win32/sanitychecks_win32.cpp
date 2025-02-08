#include "env.h"
#include "envmodule.h"
#include "sanitychecks.h"
#include "settings.h"
#include <Knownfolders.h>
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

namespace sanity
{

using namespace MOBase;

std::vector<std::pair<QString, QString>> getSystemDirectories()
{
  // folder ids and display names for logging
  const std::vector<std::pair<GUID, QString>> systemFolderIDs = {
      {FOLDERID_ProgramFiles, "in Program Files"},
      {FOLDERID_ProgramFilesX86, "in Program Files"},
      {FOLDERID_Desktop, "on the desktop"},
      {FOLDERID_OneDrive, "in OneDrive"},
      {FOLDERID_Documents, "in Documents"},
      {FOLDERID_Downloads, "in Downloads"}};

  std::vector<std::pair<QString, QString>> systemDirs;

  for (auto&& p : systemFolderIDs) {
    const auto dir = MOBase::getOptionalKnownFolder(p.first);

    if (!dir.isEmpty()) {
      auto path = QDir::toNativeSeparators(dir).toLower();
      if (!path.endsWith("\\")) {
        path += "\\";
      }

      systemDirs.push_back({path, p.second});
    }
  }

  return systemDirs;
}

int checkMicrosoftStore(const QDir& gameDir)
{
  const QStringList pathsToCheck = {
      "/ModifiableWindowsApps/",
      "/WindowsApps/",
  };
  for (auto badPath : pathsToCheck) {
    if (gameDir.path().contains(badPath)) {
      log::error("This game is not supported by Mod Organizer.");
      log::error("Games installed through the Microsoft Store will not work properly.");
      return 1;
    }
  }

  return 0;
}

}  // namespace sanity