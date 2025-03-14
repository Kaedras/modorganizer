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

int checkIncompatibleModule(const env::Module& m)
{
  int n = 0;

  n += checkBadOSDs(m);
  n += checkUsvfsIncompatibilites(m);

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

int checkBadOSDs(const env::Module& m)
{
  // these dlls seem to interfere mostly with dialogs, like the mod info
  // dialog: they render some dialogs fully white and make it impossible to
  // interact with them
  //
  // the dlls is usually loaded on startup, but there has been some reports
  // where they got loaded later, so this is also called every time a new module
  // is loaded into this process

  static const std::string nahimic = "Nahimic (also known as SonicSuite, SonicRadar, "
                                     "SteelSeries, A-Volute, etc.)";

  auto p = [](std::string re, std::string s) {
    return std::make_pair(std::regex(re, std::regex::icase), s);
  };

  static const std::vector<std::pair<std::regex, std::string>> list = {
      p("nahimic(.*)osd\\.dll", nahimic),
      p("cassini(.*)osd\\.dll", nahimic),
      p(".+devprops.*.dll", nahimic),
      p("ss2osd\\.dll", nahimic),
      p("RTSSHooks64\\.dll", "RivaTuner Statistics Server"),
      p("SSAudioOSD\\.dll", "SteelSeries Audio"),
      p("specialk64\\.dll", "SpecialK"),
      p("corsairosdhook\\.x64\\.dll", "Corsair Utility Engine"),
      p("gtii-osd64-vk\\.dll", "ASUS GPU Tweak 2"),
      p("easyhook64\\.dll", "Razer Cortex"),
      p("k_fps64\\.dll", "Razer Cortex"),
      p("fw1fontwrapper\\.dll", "Gigabyte 3D OSD"),
      p("gfxhook64\\.dll", "Gigabyte 3D OSD")};

  const QFileInfo file(m.path());
  int n = 0;

  for (auto&& p : list) {
    std::smatch m;
    const auto filename = file.fileName().toStdString();

    if (std::regex_match(filename, m, p.first)) {
      log::warn("{}", QObject::tr(
                          "%1 is loaded.\nThis program is known to cause issues with "
                          "Mod Organizer, such as freezing or blank windows. Consider "
                          "uninstalling it.")
                          .arg(QString::fromStdString(p.second)));

      log::warn("{}", file.absoluteFilePath());
      ++n;
    }
  }

  return n;
}

int checkUsvfsIncompatibilites(const env::Module& m)
{
  // these dlls seems to interfere with usvfs

  static const std::map<QString, QString> names = {
      {u"mactype64.dll"_s, u"Mactype"_s},
      {u"epclient64.dll"_s, u"Citrix ICA Client"_s}};

  const QFileInfo file(m.path());
  int n = 0;

  for (auto&& p : names) {
    if (file.fileName().compare(p.first, Qt::CaseInsensitive) == 0) {
      log::warn(
          "{}",
          QObject::tr("%1 is loaded. This program is known to cause issues with "
                      "Mod Organizer and its virtual filesystem, such script extenders "
                      "or others programs refusing to run. Consider uninstalling it.")
              .arg(p.second));

      log::warn("{}", file.absoluteFilePath());

      ++n;
    }
  }

  return n;
}

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