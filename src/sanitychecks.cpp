#include "sanitychecks.h"
#include "env.h"
#include "envmodule.h"
#include "settings.h"
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

namespace sanity
{

using namespace MOBase;

std::vector<std::pair<QString, QString>> getSystemDirectories();
int checkBlocked();
int checkIncompatibilities(const env::Environment& e);

int checkMissingFiles()
{
  // files that are likely to be eaten
#ifdef _WIN32
  static const QStringList files(
      {"helper.exe", "nxmhandler.exe", "usvfs_proxy_x64.exe", "usvfs_proxy_x86.exe",
       "usvfs_x64.dll", "usvfs_x86.dll", "loot/loot.dll", "loot/lootcli.exe"});
#else
  static const QStringList files({"helper", "nxmhandler", "loot/lootcli"});
#endif

  log::debug("  . missing files");
  const auto dir = QCoreApplication::applicationDirPath();

  int n = 0;

  for (const auto& name : files) {
    const QFileInfo file(dir + "/" + name);

    if (!file.exists()) {
      log::warn("{}", QObject::tr(
                          "'%1' seems to be missing, an antivirus may have deleted it")
                          .arg(file.absoluteFilePath()));

      ++n;
    }
  }

  return n;
}

int checkIncompatibilities(const env::Environment& e)
{
  log::debug("  . incompatibilities");

  int n = 0;

  for (auto&& m : e.loadedModules()) {
    n += checkIncompatibleModule(m);
  }

  return n;
}

int checkProtected(const QDir& d, const QString& what)
{
  static const auto systemDirs = getSystemDirectories();

  const auto path = QDir::toNativeSeparators(d.absolutePath()).toLower();

  log::debug("  . {}: {}", what, path);

  for (auto&& sd : systemDirs) {
    if (path.startsWith(sd.first)) {
      log::warn("{} is {}; this may cause issues because it's a special "
                "system folder",
                what, sd.second);

      log::debug("path '{}' starts with '{}'", path, sd.first);

      return 1;
    }
  }

  return 0;
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

int checkPaths(IPluginGame& game, const Settings& s)
{
  log::debug("checking paths");

  int n = 0;

  n += checkProtected(game.gameDirectory(), "the game");
  n += checkMicrosoftStore(game.gameDirectory());
  n += checkProtected(QApplication::applicationDirPath(), "Mod Organizer");

  if (checkProtected(s.paths().base(), "the instance base directory")) {
    ++n;
  } else {
    n += checkProtected(s.paths().downloads(), "the downloads directory");
    n += checkProtected(s.paths().mods(), "the mods directory");
    n += checkProtected(s.paths().cache(), "the cache directory");
    n += checkProtected(s.paths().profiles(), "the profiles directory");
    n += checkProtected(s.paths().overwrite(), "the overwrite directory");
  }

  return n;
}

void checkEnvironment(const env::Environment& e)
{
  log::debug("running sanity checks...");

  int n = 0;

  n += checkBlocked();
  n += checkMissingFiles();
  n += checkIncompatibilities(e);

  log::debug("sanity checks done, {}",
             (n > 0 ? "problems were found" : "everything looks okay"));
}

}  // namespace sanity
