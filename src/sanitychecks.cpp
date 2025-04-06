#include "sanitychecks.h"
#include "env.h"
#include "envmodule.h"
#include "settings.h"
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

using namespace Qt::StringLiterals;

#ifdef __unix__
// executables file types
static const QStringList FileTypes = {u"*.so"_s};
// files that are likely to be eaten
static const QStringList files({u"helper"_s, u"nxmhandler"_s, u"loot/lootcli"_s});
#else
// executables file types
static const QStringList FileTypes = {u"*.dll"_s, u"*.exe"_s};
// files that are likely to be eaten
static const QStringList files({u"helper.exe"_s, u"nxmhandler.exe"_s,
                                u"usvfs_proxy_x64.exe"_s, u"usvfs_proxy_x86.exe"_s,
                                u"usvfs_x64.dll"_s, u"usvfs_x86.dll"_s,
                                u"loot/loot.dll"_s, u"loot/lootcli.exe"_s});
#endif

namespace sanity
{

using namespace MOBase;

extern int checkIncompatibilities(const env::Environment& e);

enum class SecurityZone
{
  NoZone     = -1,
  MyComputer = 0,
  Intranet   = 1,
  Trusted    = 2,
  Internet   = 3,
  Untrusted  = 4,
};

QString toCodeName(SecurityZone z)
{
  switch (z) {
  case SecurityZone::NoZone:
    return u"NoZone"_s;
  case SecurityZone::MyComputer:
    return u"MyComputer"_s;
  case SecurityZone::Intranet:
    return u"Intranet"_s;
  case SecurityZone::Trusted:
    return u"Trusted"_s;
  case SecurityZone::Internet:
    return u"Internet"_s;
  case SecurityZone::Untrusted:
    return u"Untrusted"_s;
  default:
    return u"Unknown zone"_s;
  }
}

QString toString(SecurityZone z)
{
  return QStringLiteral("%1 (%2)").arg(toCodeName(z)).arg(static_cast<int>(z));
}

// whether the given zone is considered blocked
//
bool isZoneBlocked(SecurityZone z)
{
  switch (z) {
  case SecurityZone::Internet:
  case SecurityZone::Untrusted:
    return true;

  case SecurityZone::NoZone:
  case SecurityZone::MyComputer:
  case SecurityZone::Intranet:
  case SecurityZone::Trusted:
  default:
    return false;
  }
}

// whether the given file is blocked
//
bool isFileBlocked(const QFileInfo& fi)
{
  // name of the alternate data stream containing the zone identifier ini
  const QString ads = u"Zone.Identifier"_s;

  // key in the ini
  const QString key = u"ZoneTransfer/ZoneId"_s;

  // the path to the ADS is always `filename:Zone.Identifier`
  const QString path    = fi.absoluteFilePath();
  const QString adsPath = path % u":"_s % ads;

  QFile f(adsPath);
  if (!f.exists()) {
    // no ADS for this file
    return false;
  }

  log::debug("'{}' has an ADS for {}", path, adsPath);

  const QSettings qs(adsPath, QSettings::IniFormat);

  // looking for key
  if (!qs.contains(key)) {
    log::debug("'{}': key '{}' not found", adsPath, key);
    return false;
  }

  // getting value
  const auto v = qs.value(key);
  if (v.isNull()) {
    log::debug("'{}': key '{}' is null", adsPath, key);
    return false;
  }

  // should be an int
  bool ok      = false;
  const auto z = static_cast<SecurityZone>(v.toInt(&ok));

  if (!ok) {
    log::debug("'{}': key '{}' is not an int (value is '{}')", adsPath, key, v);
    return false;
  }

  if (!isZoneBlocked(z)) {
    // that zone is not a blocked zone
    log::debug("'{}': zone id is {}, which is fine", adsPath, toString(z));
    return false;
  }

  // file is blocked
  log::warn("{}", QObject::tr("'%1': file is blocked (%2)").arg(path, toString(z)));

  return true;
}

int checkBlockedFiles(const QDir& dir)
{
  if (!dir.exists()) {
    // shouldn't happen
    log::error("while checking for blocked files, directory '{}' not found",
               dir.absolutePath());

    return 1;
  }

  const auto files = dir.entryInfoList(FileTypes, QDir::Files);
  if (files.empty()) {
    // shouldn't happen
    log::error("while checking for blocked files, directory '{}' is empty",
               dir.absolutePath());

    return 1;
  }

  int n = 0;

  // checking each file in this directory
  for (auto&& fi : files) {
    if (isFileBlocked(fi)) {
      ++n;
    }
  }

  return n;
}

int checkBlocked()
{
  // directories that contain executables; these need to be explicit because
  // portable instances might add billions of files in MO's directory
  const QStringList dirs = {u"."_s,    u"/dlls"_s,      u"/loot"_s,
                            u"/NCC"_s, u"/platforms"_s, u"/plugins"_s};

  log::debug("  . blocked files");
  const QString appDir = QCoreApplication::applicationDirPath();

  int n = 0;

  for (const auto& d : dirs) {
    const auto path = QDir(appDir % u"/"_s % d).canonicalPath();
    n += checkBlockedFiles(path);
  }

  return n;
}

int checkMissingFiles()
{
  log::debug("  . missing files");
  const auto dir = QCoreApplication::applicationDirPath();

  int n = 0;

  for (const auto& name : files) {
    const QFileInfo file(dir % u"/"_s % name);

    if (!file.exists()) {
      log::warn("{}", QObject::tr(
                          "'%1' seems to be missing, an antivirus may have deleted it")
                          .arg(file.absoluteFilePath()));

      ++n;
    }
  }

  return n;
}

extern std::vector<std::pair<QString, QString>> getSystemDirectories();

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

extern int checkMicrosoftStore(const QDir& gameDir);

int checkPaths(IPluginGame& game, const Settings& s)
{
  log::debug("checking paths");

  int n = 0;

  n += checkProtected(game.gameDirectory(), u"the game"_s);
  n += checkMicrosoftStore(game.gameDirectory());
  n += checkProtected(QApplication::applicationDirPath(), u"Mod Organizer"_s);

  if (checkProtected(s.paths().base(), u"the instance base directory"_s)) {
    ++n;
  } else {
    n += checkProtected(s.paths().downloads(), u"the downloads directory"_s);
    n += checkProtected(s.paths().mods(), u"the mods directory"_s);
    n += checkProtected(s.paths().cache(), u"the cache directory"_s);
    n += checkProtected(s.paths().profiles(), u"the profiles directory"_s);
    n += checkProtected(s.paths().overwrite(), u"the overwrite directory"_s);
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
