/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "spawn.h"

#ifdef __unix__
#include "linux/compatibility.h"
static constexpr const char* steamName        = "steam";
static constexpr const char* steamServiceName = "steamwebhelper";
#else
static constexpr const char* steamName        = "Steam.exe";
static constexpr const char* steamServiceName = "SteamService.exe";
#endif

#include "env.h"
#include "envmodule.h"
#include "envos.h"
#include "envsecurity.h"
#include "settings.h"
#include "settingsdialogworkarounds.h"
#include "shared/appconfig.h"
#include "shared/os_error.h"
#include <QApplication>
#include <QMessageBox>
#include <QtDebug>
#include <uibase/errorcodes.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/utility.h>

using namespace MOBase;
using namespace MOShared;

namespace spawn::dialogs
{

extern QString makeDetails(const SpawnParameters& sp, DWORD code,
                           const QString& more = {});
extern QString makeContent(const SpawnParameters& sp, DWORD code,
                           const QString& message = {});

void spawnFailed(QWidget* parent, const SpawnParameters& sp, DWORD code)
{
  const auto details = makeDetails(sp, code);
  log::error("{}", details);

  const auto title = QObject::tr("Cannot launch program");

  const auto mainText = QObject::tr("Cannot start %1").arg(sp.binary.fileName());

  MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(makeContent(sp, code))
      .details(details)
      .icon(QMessageBox::Critical)
      .exec();
}

void helperFailed(QWidget* parent, DWORD code, const QString& why,
                  const QString& binary, const QString& cwd, const QString& args)
{
  SpawnParameters sp;
  sp.binary = QFileInfo(binary);
  sp.currentDirectory.setPath(cwd);
  sp.arguments = args;

  const auto details = makeDetails(sp, code, "in " + why);
  log::error("{}", details);

  const auto title = QObject::tr("Cannot launch helper");

  const auto mainText = QObject::tr("Cannot start %1").arg(sp.binary.fileName());

  MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(makeContent(sp, code))
      .details(details)
      .icon(QMessageBox::Critical)
      .exec();
}

QMessageBox::StandardButton
confirmStartSteam(QWidget* parent, const SpawnParameters& sp, const QString& details)
{
  const auto title    = QObject::tr("Launch Steam");
  const auto mainText = QObject::tr("This program requires Steam");
  const auto content  = QObject::tr(
      "Mod Organizer has detected that this program likely requires Steam to be "
       "running to function properly.");

  return MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(content)
      .details(details)
      .icon(QMessageBox::Question)
      .button({QObject::tr("Start Steam"), QMessageBox::Yes})
      .button({QObject::tr("Continue without starting Steam"),
               QObject::tr("The program might fail to run."), QMessageBox::No})
      .button({QObject::tr("Cancel"), QMessageBox::Cancel})
      .remember("steamQuery", sp.binary.fileName())
      .exec();
}

QMessageBox::StandardButton confirmRestartAsAdminForSteam(QWidget* parent,
                                                          const SpawnParameters& sp)
{
  const auto title    = QObject::tr("Elevation required");
  const auto mainText = QObject::tr("Steam is running as administrator");
  const auto content  = QObject::tr(
      "Running Steam as administrator is typically unnecessary and can cause "
       "problems when Mod Organizer itself is not running as administrator."
       "\r\n\r\n"
       "You can restart Mod Organizer as administrator and try launching the "
       "program again.");

  return MOBase::TaskDialog(parent, title)
      .main(mainText)
      .content(content)
      .icon(QMessageBox::Question)
      .button(
          {QObject::tr("Restart Mod Organizer as administrator"),
           QObject::tr("You must allow \"helper.exe\" to make changes to the system."),
           QMessageBox::Yes})
      .button({QObject::tr("Continue"), QObject::tr("The program might fail to run."),
               QMessageBox::No})
      .button({QObject::tr("Cancel"), QMessageBox::Cancel})
      .remember("steamAdminQuery", sp.binary.fileName())
      .exec();
}

QMessageBox::StandardButton
confirmBlacklisted(QWidget* parent, const SpawnParameters& sp, Settings& settings)
{
  const auto title = QObject::tr("Blacklisted program");
  const auto mainText =
      QObject::tr("The program %1 is blacklisted").arg(sp.binary.fileName());
  const auto content = QObject::tr(
      "The program you are attempting to launch is blacklisted in the virtual "
      "filesystem. This will likely prevent it from seeing any mods, INI files "
      "or any other virtualized files.");

  const auto details = "Executable: " + sp.binary.fileName() +
                       "\n"
                       "Current blacklist: " +
                       settings.executablesBlacklist();

  auto r = MOBase::TaskDialog(parent, title)
               .main(mainText)
               .content(content)
               .details(details)
               .icon(QMessageBox::Question)
               .remember("blacklistedExecutable", sp.binary.fileName())
               .button({QObject::tr("Continue"),
                        QObject::tr("Your mods might not work."), QMessageBox::Yes})
               .button({QObject::tr("Change the blacklist"), QMessageBox::Retry})
               .button({QObject::tr("Cancel"), QMessageBox::Cancel})
               .exec();

  if (r == QMessageBox::Retry) {
    if (!WorkaroundsSettingsTab::changeBlacklistNow(parent, settings)) {
      r = QMessageBox::Cancel;
    }
  }

  return r;
}

}  // namespace spawn::dialogs

namespace spawn
{

// functions with platform specific implementations
extern void logSpawning(const SpawnParameters& sp, const QString& realCmd);
extern bool startSteam(QWidget* parent);
extern bool restartAsAdmin(QWidget* parent);
extern void startBinaryAdmin(QWidget* parent, const SpawnParameters& sp);

struct SteamStatus
{
  bool running    = false;
  bool accessible = false;
};

SteamStatus getSteamStatus()
{
  SteamStatus ss;

  const auto ps = env::Environment().runningProcesses();

  for (const auto& p : ps) {
    if ((p.name().compare(steamName, Qt::CaseInsensitive) == 0) ||
        (p.name().compare(steamServiceName, Qt::CaseInsensitive) == 0)) {
      ss.running    = true;
      ss.accessible = p.canAccess();

      log::debug("'{}' is running, accessible={}", p.name(),
                 (ss.accessible ? "yes" : "no"));

      break;
    }
  }

  return ss;
}

QString makeSteamArguments(const QString& username, const QString& password)
{
  QString args;

  if (username != "") {
    args += "-login " + username;

    if (password != "") {
      args += " " + password;
    }
  }

  return args;
}

bool checkSteam(QWidget* parent, const SpawnParameters& sp, const QDir& gameDirectory,
                const QString& steamAppID, const Settings& settings)
{
  static const std::vector<QString> steamFiles = {"steam_api.dll", "steam_api64.dll"};

  log::debug("checking steam");

  if (!steamAppID.isEmpty()) {
    env::set("SteamAPPId", steamAppID);
  } else {
    env::set("SteamAPPId", settings.steam().appID());
  }

  bool steamRequired = false;
  QString details;

  for (const auto& file : steamFiles) {
    const QFileInfo fi(gameDirectory.absoluteFilePath(file));
    if (fi.exists()) {
      details = QString("managed game is located at '%1' and file '%2' exists")
                    .arg(gameDirectory.absolutePath())
                    .arg(fi.absoluteFilePath());

      log::debug("{}", details);
      steamRequired = true;

      break;
    }
  }

  if (!steamRequired) {
    log::debug("program doesn't seem to require steam");
    return true;
  }

  auto ss = getSteamStatus();

  if (!ss.running) {
    log::debug("steam isn't running, asking to start steam");

    const auto c = dialogs::confirmStartSteam(parent, sp, details);

    if (c == QDialogButtonBox::Yes) {
      log::debug("user wants to start steam");

      if (!startSteam(parent)) {
        // cancel
        return false;
      }

      // double-check that Steam is started
      ss = getSteamStatus();
      if (!ss.running) {
        log::error("steam is still not running, hoping for the best");
        return true;
      }
    } else if (c == QDialogButtonBox::No) {
      log::debug("user declined to start steam");
      return true;
    } else {
      log::debug("user cancelled");
      return false;
    }
  }

  if (ss.running && !ss.accessible) {
    log::debug("steam is running but is not accessible, asking to restart MO");
    const auto c = dialogs::confirmRestartAsAdminForSteam(parent, sp);

    if (c == QDialogButtonBox::Yes) {
      restartAsAdmin(parent);
      return false;
    } else if (c == QDialogButtonBox::No) {
      log::debug("user declined to restart MO, continuing");
      return true;
    } else {
      log::debug("user cancelled");
      return false;
    }
  }

  return true;
}

bool checkBlacklist(QWidget* parent, const SpawnParameters& sp, Settings& settings)
{
  for (;;) {
    if (!settings.isExecutableBlacklisted(sp.binary.fileName())) {
      return true;
    }

    const auto r = dialogs::confirmBlacklisted(parent, sp, settings);

    if (r != QMessageBox::Retry) {
      return (r == QMessageBox::Yes);
    }
  }
}

bool isJavaFile(const QFileInfo& target)
{
  return (target.suffix().compare("jar", Qt::CaseInsensitive) == 0);
}

}  // namespace spawn

namespace helper
{

extern bool helperExec(QWidget* parent, const QString& moDirectory,
                       const QString& commandLine, bool async);

bool backdateBSAs(QWidget* parent, const QString& moPath, const QString& dataPath)
{
  const QString commandLine = QString(R"(backdateBSA "%1")").arg(dataPath);

  return helperExec(parent, moPath, commandLine, false);
}

}  // namespace helper
