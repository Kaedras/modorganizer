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

#include "settings.h"
#include "env.h"
#include "envmetrics.h"
#include "executableslist.h"
#include "modelutils.h"
#include "serverinfo.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <expanderwidget.h>
#include <iplugingame.h>
#include <utility.h>

using namespace MOBase;
using namespace MOShared;
using namespace Qt::StringLiterals;

EndorsementState endorsementStateFromString(const QString& s)
{
  if (s == "Endorsed"_L1) {
    return EndorsementState::Accepted;
  } else if (s == "Abstained"_L1) {
    return EndorsementState::Refused;
  } else {
    return EndorsementState::NoDecision;
  }
}

QString toString(EndorsementState s)
{
  switch (s) {
  case EndorsementState::Accepted:
    return u"Endorsed"_s;

  case EndorsementState::Refused:
    return u"Abstained"_s;

  case EndorsementState::NoDecision:  // fall-through
  default:
    return {};
  }
}

Settings* Settings::s_Instance = nullptr;

Settings::Settings(const QString& path, bool globalInstance)
    : m_Settings(path, QSettings::IniFormat), m_Game(m_Settings),
      m_Geometry(m_Settings), m_Widgets(m_Settings, globalInstance),
      m_Colors(m_Settings), m_Plugins(m_Settings), m_Paths(m_Settings),
      m_Network(m_Settings, globalInstance), m_Nexus(*this, m_Settings),
      m_Steam(*this, m_Settings), m_Interface(m_Settings), m_Diagnostics(m_Settings)
{
  if (globalInstance) {
    if (s_Instance != nullptr) {
      throw std::runtime_error("second instance of \"Settings\" created");
    } else {
      s_Instance = this;
    }
  }
}

Settings::~Settings()
{
  if (s_Instance == this) {
    MOBase::QuestionBoxMemory::setCallbacks({}, {}, {});
    s_Instance = nullptr;
  }
}

Settings& Settings::instance()
{
  if (s_Instance == nullptr) {
    throw std::runtime_error("no instance of \"Settings\"");
  }

  return *s_Instance;
}

Settings* Settings::maybeInstance()
{
  return s_Instance;
}

void Settings::processUpdates(const QVersionNumber& currentVersion,
                              const QVersionNumber& lastVersion)
{
  if (firstStart()) {
    set(m_Settings, u"General"_s, u"version"_s, currentVersion.toString());
    return;
  }

  if (currentVersion == lastVersion) {
    return;
  }

  log::info("updating from {} to {}", lastVersion.toString(),
            currentVersion.toString());

  auto version = [&](const QVersionNumber& v, auto&& f) {
    if (lastVersion < v) {
      log::debug("processing updates for {}", v.toString());
      f();
    }
  };

  version({2, 2, 0}, [&] {
    remove(m_Settings, u"Settings"_s, u"steam_password"_s);
    remove(m_Settings, u"Settings"_s, u"nexus_username"_s);
    remove(m_Settings, u"Settings"_s, u"nexus_password"_s);
    remove(m_Settings, u"Settings"_s, u"nexus_login"_s);
    remove(m_Settings, u"Settings"_s, u"nexus_api_key"_s);
    remove(m_Settings, u"Settings"_s, u"ask_for_nexuspw"_s);
    remove(m_Settings, u"Settings"_s, u"nmm_version"_s);

    removeSection(m_Settings, u"Servers"_s);
  });

  version({2, 2, 1}, [&] {
    remove(m_Settings, u"General"_s, u"mod_info_tabs"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflict_expanders"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts"_s);
    remove(m_Settings, u"General"_s, u"mod_info_advanced_conflicts"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_overwrite"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_noconflict"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_overwritten"_s);
  });

  version({2, 2, 2}, [&] {
    // log splitter is gone, it's a dock now
    remove(m_Settings, u"General"_s, u"log_split"_s);

    // moved to widgets
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_tab"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_general_expanders"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_general_overwrite"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_general_noconflict"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_general_overwritten"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_advanced_list"_s);
    remove(m_Settings, u"General"_s, u"mod_info_conflicts_advanced_options"_s);
    remove(m_Settings, u"General"_s, u"mod_info_tab_order"_s);
    remove(m_Settings, u"General"_s, u"mod_info_dialog_images_show_dds"_s);

    // moved to geometry
    remove(m_Settings, u"General"_s, u"window_geometry"_s);
    remove(m_Settings, u"General"_s, u"window_state"_s);
    remove(m_Settings, u"General"_s, u"toolbar_size"_s);
    remove(m_Settings, u"General"_s, u"toolbar_button_style"_s);
    remove(m_Settings, u"General"_s, u"menubar_visible"_s);
    remove(m_Settings, u"General"_s, u"statusbar_visible"_s);
    remove(m_Settings, u"General"_s, u"window_split"_s);
    remove(m_Settings, u"General"_s, u"window_monitor"_s);
    remove(m_Settings, u"General"_s, u"browser_geometry"_s);
    remove(m_Settings, u"General"_s, u"filters_visible"_s);

    // this was supposed to have been removed above when updating from 2.2.0,
    // but it wasn't in Settings, it was in General
    remove(m_Settings, u"General"_s, u"ask_for_nexuspw"_s);

    m_Network.updateFromOldMap();
  });

  version({2, 4, 0}, [&] {
    // removed
    remove(m_Settings, u"Settings"_s, u"hide_unchecked_plugins"_s);
    remove(m_Settings, u"Settings"_s, u"load_mechanism"_s);
  });

  // save version in all case
  set(m_Settings, u"General"_s, u"version"_s, currentVersion.toString());

  log::debug("updating done");
}

QString Settings::filename() const
{
  return m_Settings.fileName();
}

bool Settings::checkForUpdates() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"check_for_updates"_s, true);
}

void Settings::setCheckForUpdates(bool b)
{
  set(m_Settings, u"Settings"_s, u"check_for_updates"_s, b);
}

bool Settings::usePrereleases() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"use_prereleases"_s, false);
}

void Settings::setUsePrereleases(bool b)
{
  set(m_Settings, u"Settings"_s, u"use_prereleases"_s, b);
}

bool Settings::profileLocalInis() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"profile_local_inis"_s, true);
}

void Settings::setProfileLocalInis(bool b)
{
  set(m_Settings, u"Settings"_s, u"profile_local_inis"_s, b);
}

bool Settings::profileLocalSaves() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"profile_local_saves"_s, false);
}

void Settings::setProfileLocalSaves(bool b)
{
  set(m_Settings, u"Settings"_s, u"profile_local_saves"_s, b);
}

bool Settings::profileArchiveInvalidation() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"profile_archive_invalidation"_s, false);
}

void Settings::setProfileArchiveInvalidation(bool b)
{
  set(m_Settings, u"Settings"_s, u"profile_archive_invalidation"_s, b);
}

bool Settings::useSplash() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"use_splash"_s, true);
}

void Settings::setUseSplash(bool b)
{
  set(m_Settings, u"Settings"_s, u"use_splash"_s, b);
}

std::size_t Settings::refreshThreadCount() const
{
  return get<std::size_t>(m_Settings, u"Settings"_s, u"refresh_thread_count"_s, 10);
}

void Settings::setRefreshThreadCount(std::size_t n) const
{
  return set(m_Settings, u"Settings"_s, u"refresh_thread_count"_s,
             QVariant::fromValue(n));
}

std::optional<QVersionNumber> Settings::version() const
{
  if (auto v = getOptional<QString>(m_Settings, u"General"_s, u"version"_s)) {
    return QVersionNumber::fromString(*v).normalized();
  }

  return {};
}

bool Settings::firstStart() const
{
  return get<bool>(m_Settings, u"General"_s, u"first_start"_s, true);
}

void Settings::setFirstStart(bool b)
{
  set(m_Settings, u"General"_s, u"first_start"_s, b);
}

QString Settings::executablesBlacklist() const
{
  static const QString def =
      (QStringList() << u"Chrome.exe"_s << u"Firefox.exe"_s << u"TSVNCache.exe"_s
                     << u"TGitCache.exe"_s << u"Steam.exe"_s << u"GameOverlayUI.exe"_s
                     << u"Discord.exe"_s << u"GalaxyClient.exe"_s << u"Spotify.exe"_s
                     << u"Brave.exe"_s)
          .join(';');

  return get<QString>(m_Settings, u"Settings"_s, u"executable_blacklist"_s, def);
}

bool Settings::isExecutableBlacklisted(const QString& s) const
{
  for (const auto& exec : executablesBlacklist().split(';')) {
    if (exec.compare(s, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }

  return false;
}

void Settings::setExecutablesBlacklist(const QString& s)
{
  set(m_Settings, u"Settings"_s, u"executable_blacklist"_s, s);
}

QStringList Settings::skipFileSuffixes() const
{
  static const QStringList def = QStringList() << u".mohidden"_s;

  auto setting =
      get<QStringList>(m_Settings, u"Settings"_s, u"skip_file_suffixes"_s, def);

  return setting;
}

void Settings::setSkipFileSuffixes(const QStringList& s)
{
  set(m_Settings, u"Settings"_s, u"skip_file_suffixes"_s, s);
}

QStringList Settings::skipDirectories() const
{
  static const QStringList def = QStringList() << u".git"_s;

  auto setting =
      get<QStringList>(m_Settings, u"Settings"_s, u"skip_directories"_s, def);

  return setting;
}

void Settings::setSkipDirectories(const QStringList& s)
{
  set(m_Settings, u"Settings"_s, u"skip_directories"_s, s);
}

void Settings::setMotdHash(uint hash)
{
  set(m_Settings, u"General"_s, u"motd_hash"_s, hash);
}

unsigned int Settings::motdHash() const
{
  return get<unsigned int>(m_Settings, u"General"_s, u"motd_hash"_s, 0);
}

bool Settings::archiveParsing() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"archive_parsing_experimental"_s, false);
}

void Settings::setArchiveParsing(bool b)
{
  set(m_Settings, u"Settings"_s, u"archive_parsing_experimental"_s, b);
}

std::vector<std::map<QString, QVariant>> Settings::executables() const
{
  ScopedReadArray sra(m_Settings, u"customExecutables"_s);
  std::vector<std::map<QString, QVariant>> v;

  sra.for_each([&] {
    std::map<QString, QVariant> map;

    for (auto&& key : sra.keys()) {
      map[key] = sra.get<QVariant>(key);
    }

    v.push_back(map);
  });

  return v;
}

void Settings::setExecutables(const std::vector<std::map<QString, QVariant>>& v)
{
  const auto current = executables();

  if (current == v) {
    // no change
    return;
  }

  if (current.size() > v.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, u"customExecutables"_s);
  }

  ScopedWriteArray swa(m_Settings, u"customExecutables"_s, v.size());

  for (const auto& map : v) {
    swa.next();

    for (auto&& p : map) {
      swa.set(p.first, p.second);
    }
  }
}

bool Settings::keepBackupOnInstall() const
{
  return get<bool>(m_Settings, u"General"_s, u"backup_install"_s, false);
}

void Settings::setKeepBackupOnInstall(bool b)
{
  set(m_Settings, u"General"_s, u"backup_install"_s, b);
}

GameSettings& Settings::game()
{
  return m_Game;
}

const GameSettings& Settings::game() const
{
  return m_Game;
}

GeometrySettings& Settings::geometry()
{
  return m_Geometry;
}

const GeometrySettings& Settings::geometry() const
{
  return m_Geometry;
}

WidgetSettings& Settings::widgets()
{
  return m_Widgets;
}

const WidgetSettings& Settings::widgets() const
{
  return m_Widgets;
}

ColorSettings& Settings::colors()
{
  return m_Colors;
}

const ColorSettings& Settings::colors() const
{
  return m_Colors;
}

PluginSettings& Settings::plugins()
{
  return m_Plugins;
}

const PluginSettings& Settings::plugins() const
{
  return m_Plugins;
}

PathSettings& Settings::paths()
{
  return m_Paths;
}

const PathSettings& Settings::paths() const
{
  return m_Paths;
}

NetworkSettings& Settings::network()
{
  return m_Network;
}

const NetworkSettings& Settings::network() const
{
  return m_Network;
}

NexusSettings& Settings::nexus()
{
  return m_Nexus;
}

const NexusSettings& Settings::nexus() const
{
  return m_Nexus;
}

SteamSettings& Settings::steam()
{
  return m_Steam;
}

const SteamSettings& Settings::steam() const
{
  return m_Steam;
}

InterfaceSettings& Settings::interface()
{
  return m_Interface;
}

const InterfaceSettings& Settings::interface() const
{
  return m_Interface;
}

DiagnosticsSettings& Settings::diagnostics()
{
  return m_Diagnostics;
}

const DiagnosticsSettings& Settings::diagnostics() const
{
  return m_Diagnostics;
}

QSettings::Status Settings::sync() const
{
  m_Settings.sync();

  const auto s = m_Settings.status();

  // there's a bug in Qt at least until 5.15.0 where a utf-8 bom in the ini is
  // handled correctly but still sets FormatError
  //
  // see qsettings.cpp, in QConfFileSettingsPrivate::readIniFile(), there's a
  // specific check for utf-8, which adjusts `dataPos` so it's skipped, but
  // the FLUSH_CURRENT_SECTION() macro uses `currentSectionStart`, and that one
  // isn't adjusted when changing `dataPos` on the first line and so stays 0
  //
  // this puts the bom in `unparsedIniSections` and eventually sets FormatError
  // somewhere
  //
  //
  // the other problem is that the status is never reset, not even when calling
  // sync(), so the FormatError that's returned here is actually from reading
  // the ini, not writing it
  //
  //
  // since it's impossible to get a FormatError on write, it's considered to
  // be a NoError here

  if (s == QSettings::FormatError) {
    return QSettings::NoError;
  } else {
    return s;
  }
}

QSettings::Status Settings::iniStatus() const
{
  return m_Settings.status();
}

void Settings::dump() const
{
  static const QStringList ignore({u"username"_s, u"password"_s, u"nexus_api_key"_s,
                                   u"nexus_username"_s, u"nexus_password"_s,
                                   u"steam_username"_s});

  log::debug("settings:");

  {
    ScopedGroup sg(m_Settings, u"Settings"_s);

    for (auto k : m_Settings.allKeys()) {
      if (ignore.contains(k, Qt::CaseInsensitive)) {
        continue;
      }

      log::debug("  . {}={}", k, m_Settings.value(k).toString());
    }
  }

  m_Network.dump();
  m_Nexus.dump();
}

void Settings::managedGameChanged(IPluginGame const* gamePlugin)
{
  m_Game.setPlugin(gamePlugin);
}

GameSettings::GameSettings(QSettings& settings)
    : m_Settings(settings), m_GamePlugin(nullptr)
{}

const MOBase::IPluginGame* GameSettings::plugin()
{
  return m_GamePlugin;
}

void GameSettings::setPlugin(const MOBase::IPluginGame* gamePlugin)
{
  m_GamePlugin = gamePlugin;
}

bool GameSettings::forceEnableCoreFiles() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"force_enable_core_files"_s, true);
}

void GameSettings::setForceEnableCoreFiles(bool b)
{
  set(m_Settings, u"Settings"_s, u"force_enable_core_files"_s, b);
}

std::optional<QString> GameSettings::directory() const
{
  if (auto v = getOptional<QByteArray>(m_Settings, u"General"_s, u"gamePath"_s)) {
    return QString::fromUtf8(*v);
  }

  return {};
}

void GameSettings::setDirectory(const QString& path)
{
  set(m_Settings, u"General"_s, u"gamePath"_s, QDir::toNativeSeparators(path).toUtf8());
}

std::optional<QString> GameSettings::name() const
{
  return getOptional<QString>(m_Settings, u"General"_s, u"gameName"_s);
}

void GameSettings::setName(const QString& name)
{
  set(m_Settings, u"General"_s, u"gameName"_s, name);
}

std::optional<QString> GameSettings::edition() const
{
  return getOptional<QString>(m_Settings, u"General"_s, u"game_edition"_s);
}

void GameSettings::setEdition(const QString& name)
{
  set(m_Settings, u"General"_s, u"game_edition"_s, name);
}

std::optional<QString> GameSettings::selectedProfileName() const
{
  if (auto v =
          getOptional<QByteArray>(m_Settings, u"General"_s, u"selected_profile"_s)) {
    return QString::fromUtf8(*v);
  }

  return {};
}

void GameSettings::setSelectedProfileName(const QString& name)
{
  set(m_Settings, u"General"_s, u"selected_profile"_s, name.toUtf8());
}

GeometrySettings::GeometrySettings(QSettings& s) : m_Settings(s), m_Reset(false) {}

void GeometrySettings::requestReset()
{
  m_Reset = true;
}

void GeometrySettings::resetIfNeeded()
{
  if (!m_Reset) {
    return;
  }

  removeSection(m_Settings, u"Geometry"_s);
}

void GeometrySettings::saveGeometry(const QMainWindow* w)
{
  saveWindowGeometry(w);
}

bool GeometrySettings::restoreGeometry(QMainWindow* w) const
{
  return restoreWindowGeometry(w);
}

void GeometrySettings::saveGeometry(const QDialog* d)
{
  saveWindowGeometry(d);
}

bool GeometrySettings::restoreGeometry(QDialog* d) const
{
  const auto r = restoreWindowGeometry(d);

  if (centerDialogs()) {
    centerOnParent(d);
  }

  return r;
}

void GeometrySettings::saveWindowGeometry(const QWidget* w)
{
  set(m_Settings, u"Geometry"_s, geoSettingName(w), w->saveGeometry());
}

bool GeometrySettings::restoreWindowGeometry(QWidget* w) const
{
  if (auto v = getOptional<QByteArray>(m_Settings, u"Geometry"_s, geoSettingName(w))) {
    w->restoreGeometry(*v);
    ensureWindowOnScreen(w);
    return true;
  }

  return false;
}

void GeometrySettings::ensureWindowOnScreen(QWidget* w) const
{
  // users report that the main window and/or dialogs are displayed off-screen;
  // the usual workaround is keyboard navigation to move it
  //
  // qt should have code that deals with multiple monitors and off-screen
  // geometries, but there seems to be bugs or inconsistencies that can't be
  // reproduced
  //
  // the closest would probably be https://bugreports.qt.io/browse/QTBUG-64498,
  // which is about multiple monitors and high dpi, but it seems fixed as of
  // 5.12.4, which is shipped with 2.2.1
  //
  // without being to reproduce the problem, some simple checks are made in a
  // timer, which may mitigate the issues

  QTimer::singleShot(100, w, [w] {
    const auto borders = 20;

    // desktop geometry, made smaller to make sure there isn't just a few pixels
    const auto originalDg = env::Environment().metrics().desktopGeometry();
    const auto dg         = originalDg.adjusted(borders, borders, -borders, -borders);

    const auto g = w->geometry();

    if (!dg.intersects(g)) {
      log::warn("window '{}' is offscreen, moving to main monitor; geo={}, desktop={}",
                w->objectName(), g, originalDg);

      // widget is off-screen, center it on main monitor
      centerOnMonitor(w, -1);

      log::warn("window '{}' now at {}", w->objectName(), w->geometry());
    }
  });
}

void GeometrySettings::saveState(const QMainWindow* w)
{
  set(m_Settings, u"Geometry"_s, stateSettingName(w), w->saveState());
}

bool GeometrySettings::restoreState(QMainWindow* w) const
{
  if (auto v =
          getOptional<QByteArray>(m_Settings, u"Geometry"_s, stateSettingName(w))) {
    w->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveState(const QHeaderView* w)
{
  set(m_Settings, u"Geometry"_s, stateSettingName(w), w->saveState());
}

bool GeometrySettings::restoreState(QHeaderView* w) const
{
  if (auto v =
          getOptional<QByteArray>(m_Settings, u"Geometry"_s, stateSettingName(w))) {
    w->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveState(const QSplitter* w)
{
  set(m_Settings, u"Geometry"_s, stateSettingName(w), w->saveState());
}

bool GeometrySettings::restoreState(QSplitter* w) const
{
  if (auto v =
          getOptional<QByteArray>(m_Settings, u"Geometry"_s, stateSettingName(w))) {
    w->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveState(const ExpanderWidget* expander)
{
  set(m_Settings, u"Geometry"_s, stateSettingName(expander), expander->saveState());
}

bool GeometrySettings::restoreState(ExpanderWidget* expander) const
{
  if (auto v = getOptional<QByteArray>(m_Settings, u"Geometry"_s,
                                       stateSettingName(expander))) {
    expander->restoreState(*v);
    return true;
  }

  return false;
}

void GeometrySettings::saveVisibility(const QWidget* w)
{
  set(m_Settings, u"Geometry"_s, visibilitySettingName(w), w->isVisible());
}

bool GeometrySettings::restoreVisibility(QWidget* w, std::optional<bool> def) const
{
  if (auto v =
          getOptional<bool>(m_Settings, u"Geometry"_s, visibilitySettingName(w), def)) {
    w->setVisible(*v);
    return true;
  }

  return false;
}

void GeometrySettings::restoreToolbars(QMainWindow* w) const
{
  // all toolbars have the same size and button style settings
  const auto size = getOptional<QSize>(m_Settings, u"Geometry"_s, u"toolbar_size"_s);
  const auto style =
      getOptional<int>(m_Settings, u"Geometry"_s, u"toolbar_button_style"_s);

  for (auto* tb : w->findChildren<QToolBar*>()) {
    if (size) {
      tb->setIconSize(*size);
    }

    if (style) {
      tb->setToolButtonStyle(static_cast<Qt::ToolButtonStyle>(*style));
    }

    restoreVisibility(tb);
  }
}

void GeometrySettings::saveToolbars(const QMainWindow* w)
{
  const auto tbs = w->findChildren<QToolBar*>();

  // save visibility for all
  for (auto* tb : tbs) {
    saveVisibility(tb);
  }

  // all toolbars have the same size and button style settings, just save the
  // first one
  if (!tbs.isEmpty()) {
    const auto* tb = tbs[0];

    set(m_Settings, u"Geometry"_s, u"toolbar_size"_s, tb->iconSize());
    set(m_Settings, u"Geometry"_s, u"toolbar_button_style"_s,
        static_cast<int>(tb->toolButtonStyle()));
  }
}

QStringList GeometrySettings::modInfoTabOrder() const
{
  QStringList v;

  if (m_Settings.contains("mod_info_tabs"_L1)) {
    // old byte array from 2.2.0
    QDataStream stream(m_Settings.value(u"mod_info_tabs"_s).toByteArray());

    int count = 0;
    stream >> count;

    for (int i = 0; i < count; ++i) {
      QString s;
      stream >> s;
      v.push_back(s);
    }
  } else {
    // string list since 2.2.1
    QString string = get<QString>(m_Settings, u"Widgets"_s, u"ModInfoTabOrder"_s, "");
    QTextStream stream(&string);

    while (!stream.atEnd()) {
      QString s;
      stream >> s;
      v.push_back(s);
    }
  }

  return v;
}

void GeometrySettings::setModInfoTabOrder(const QString& names)
{
  set(m_Settings, u"Widgets"_s, u"ModInfoTabOrder"_s, names);
}

bool GeometrySettings::centerDialogs() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"center_dialogs"_s, false);
}

void GeometrySettings::setCenterDialogs(bool b)
{
  set(m_Settings, u"Settings"_s, u"center_dialogs"_s, b);
}

void GeometrySettings::centerOnMainWindowMonitor(QWidget* w) const
{
  const auto monitor =
      getOptional<int>(m_Settings, u"Geometry"_s, u"MainWindow_monitor"_s).value_or(-1);

  centerOnMonitor(w, monitor);
}

void GeometrySettings::centerOnMonitor(QWidget* w, int monitor)
{
  QPoint center;

  if (monitor >= 0 && monitor < QGuiApplication::screens().size()) {
    center = QGuiApplication::screens().at(monitor)->geometry().center();
  } else {
    center = QGuiApplication::primaryScreen()->geometry().center();
  }

  w->move(center - w->rect().center());
}

void GeometrySettings::centerOnParent(QWidget* w, QWidget* parent)
{
  if (!parent) {
    parent = w->parentWidget();

    if (!parent) {
      parent = qApp->activeWindow();
    }
  }

  if (parent && parent->isVisible()) {
    const auto pr = parent->geometry();
    w->move(pr.center() - w->rect().center());
  }
}

void GeometrySettings::saveMainWindowMonitor(const QMainWindow* w)
{
  if (auto* handle = w->windowHandle()) {
    if (auto* screen = handle->screen()) {
      const int screenId = QGuiApplication::screens().indexOf(screen);
      set(m_Settings, u"Geometry"_s, u"MainWindow_monitor"_s, screenId);
    }
  }
}

Qt::Orientation dockOrientation(const QMainWindow* mw, const QDockWidget* d)
{
  // docks in these areas are horizontal
  const auto horizontalAreas = Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea;

  if (mw->dockWidgetArea(const_cast<QDockWidget*>(d)) & horizontalAreas) {
    return Qt::Horizontal;
  } else {
    return Qt::Vertical;
  }
}

void GeometrySettings::saveDocks(const QMainWindow* mw)
{
  // this attempts to fix https://bugreports.qt.io/browse/QTBUG-46620 where dock
  // sizes are not restored when the main window is maximized; it is used in
  // MainWindow::readSettings() and MainWindow::storeSettings()
  //
  // there's also https://stackoverflow.com/questions/44005852, which has what
  // seems to be a popular fix, but it breaks the restored size of the window
  // by setting it to the desktop's resolution, so that doesn't work
  //
  // the only fix I could find is to remember the sizes of the docks and manually
  // setting them back; saving is straightforward, but restoring is messy
  //
  // this also depends on the window being visible before the timer in restore()
  // is fired and the timer must be processed by application.exec(); therefore,
  // the splash screen _must_ be closed before readSettings() is called, because
  // it has its own event loop, which seems to interfere with this
  //
  // all of this should become unnecessary when QTBUG-46620 is fixed
  //

  // saves the size of each dock
  for (const auto* dock : mw->findChildren<QDockWidget*>()) {
    int size = 0;

    // save the width for horizontal docks, or the height for vertical
    if (dockOrientation(mw, dock) == Qt::Horizontal) {
      size = dock->size().width();
    } else {
      size = dock->size().height();
    }

    set(m_Settings, u"Geometry"_s, dockSettingName(dock), size);
  }
}

void GeometrySettings::restoreDocks(QMainWindow* mw) const
{
  struct DockInfo
  {
    QDockWidget* d;
    int size = 0;
    Qt::Orientation ori;
  };

  std::vector<DockInfo> dockInfos;

  // for each dock
  for (auto* dock : mw->findChildren<QDockWidget*>()) {
    if (auto size =
            getOptional<int>(m_Settings, u"Geometry"_s, dockSettingName(dock))) {
      // remember this dock, its size and orientation
      dockInfos.push_back({dock, *size, dockOrientation(mw, dock)});
    }
  }

  // the main window must have had time to process the settings from
  // readSettings() or it seems to override whatever is set here
  //
  // some people said a single processEvents() call is enough, but it doesn't
  // look like it
  QTimer::singleShot(5, [=] {
    for (const auto& info : dockInfos) {
      mw->resizeDocks({info.d}, {info.size}, info.ori);
    }
  });
}

WidgetSettings::WidgetSettings(QSettings& s, bool globalInstance) : m_Settings(s)
{
  if (globalInstance) {
    MOBase::QuestionBoxMemory::setCallbacks(
        [this](auto&& w, auto&& f) {
          return questionButton(w, f);
        },
        [this](auto&& w, auto&& b) {
          setQuestionWindowButton(w, b);
        },
        [this](auto&& w, auto&& f, auto&& b) {
          setQuestionFileButton(w, f, b);
        });
  }
}

void WidgetSettings::saveTreeCheckState(const QTreeView* tv, int role)
{
  QVariantList data;
  for (auto index : flatIndex(tv->model())) {
    data.append(index.data(role));
  }
  set(m_Settings, u"Widgets"_s, indexSettingName(tv), data);
}

void WidgetSettings::restoreTreeCheckState(QTreeView* tv, int role) const
{
  if (auto states =
          getOptional<QVariantList>(m_Settings, u"Widgets"_s, indexSettingName(tv))) {
    auto allIndex = flatIndex(tv->model());
    MOBase::log::debug("restoreTreeCheckState: {}, {}", states->size(),
                       allIndex.size());
    if (states->size() != allIndex.size()) {
      return;
    }
    for (int i = 0; i < states->size(); ++i) {
      tv->model()->setData(allIndex[i], states->at(i), role);
    }
  }
}

void WidgetSettings::saveTreeExpandState(const QTreeView* tv, int role)
{
  QVariantList expanded;
  for (auto index : flatIndex(tv->model())) {
    if (tv->isExpanded(index)) {
      expanded.append(index.data(role));
    }
  }
  set(m_Settings, u"Widgets"_s, indexSettingName(tv), expanded);
}

void WidgetSettings::restoreTreeExpandState(QTreeView* tv, int role) const
{
  if (auto expanded =
          getOptional<QVariantList>(m_Settings, u"Widgets"_s, indexSettingName(tv))) {
    tv->collapseAll();
    for (auto index : flatIndex(tv->model())) {
      if (expanded->contains(index.data(role))) {
        tv->expand(index);
      }
    }
  }
}

std::optional<int> WidgetSettings::index(const QComboBox* cb) const
{
  return getOptional<int>(m_Settings, u"Widgets"_s, indexSettingName(cb));
}

void WidgetSettings::saveIndex(const QComboBox* cb)
{
  set(m_Settings, u"Widgets"_s, indexSettingName(cb), cb->currentIndex());
}

void WidgetSettings::restoreIndex(QComboBox* cb, std::optional<int> def) const
{
  if (auto v = getOptional<int>(m_Settings, u"Widgets"_s, indexSettingName(cb), def)) {
    cb->setCurrentIndex(*v);
  }
}

std::optional<int> WidgetSettings::index(const QTabWidget* w) const
{
  return getOptional<int>(m_Settings, u"Widgets"_s, indexSettingName(w));
}

void WidgetSettings::saveIndex(const QTabWidget* w)
{
  set(m_Settings, u"Widgets"_s, indexSettingName(w), w->currentIndex());
}

void WidgetSettings::restoreIndex(QTabWidget* w, std::optional<int> def) const
{
  if (auto v = getOptional<int>(m_Settings, u"Widgets"_s, indexSettingName(w), def)) {
    w->setCurrentIndex(*v);
  }
}

std::optional<bool> WidgetSettings::checked(const QAbstractButton* w) const
{
  warnIfNotCheckable(w);
  return getOptional<bool>(m_Settings, u"Widgets"_s, checkedSettingName(w));
}

void WidgetSettings::saveChecked(const QAbstractButton* w)
{
  warnIfNotCheckable(w);
  set(m_Settings, u"Widgets"_s, checkedSettingName(w), w->isChecked());
}

void WidgetSettings::restoreChecked(QAbstractButton* w, std::optional<bool> def) const
{
  warnIfNotCheckable(w);

  if (auto v =
          getOptional<bool>(m_Settings, u"Widgets"_s, checkedSettingName(w), def)) {
    w->setChecked(*v);
  }
}

QuestionBoxMemory::Button WidgetSettings::questionButton(const QString& windowName,
                                                         const QString& filename) const
{
  const QString sectionName(u"DialogChoices"_s);

  if (!filename.isEmpty()) {
    const QString fileSetting = windowName % u"/"_s % filename;
    if (auto v = getOptional<int>(m_Settings, sectionName, fileSetting)) {
      return static_cast<QuestionBoxMemory::Button>(*v);
    }
  }

  if (auto v = getOptional<int>(m_Settings, sectionName, windowName)) {
    return static_cast<QuestionBoxMemory::Button>(*v);
  }

  return QuestionBoxMemory::NoButton;
}

void WidgetSettings::setQuestionWindowButton(const QString& windowName,
                                             QuestionBoxMemory::Button button)
{
  const QString sectionName(u"DialogChoices"_s);

  if (button == QuestionBoxMemory::NoButton) {
    remove(m_Settings, sectionName, windowName);
  } else {
    set(m_Settings, sectionName, windowName, button);
  }
}

void WidgetSettings::setQuestionFileButton(const QString& windowName,
                                           const QString& filename,
                                           QuestionBoxMemory::Button button)
{
  const QString sectionName(u"DialogChoices"_s);
  const QString settingName(windowName % u"/"_s % filename);

  if (button == QuestionBoxMemory::NoButton) {
    remove(m_Settings, sectionName, settingName);
  } else {
    set(m_Settings, sectionName, settingName, button);
  }
}

void WidgetSettings::resetQuestionButtons()
{
  removeSection(m_Settings, u"DialogChoices"_s);
}

ColorSettings::ColorSettings(QSettings& s) : m_Settings(s) {}

QColor ColorSettings::modlistOverwrittenLoose() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"overwrittenLooseFilesColor"_s,
                     QColor(0, 255, 0, 64));
}

void ColorSettings::setModlistOverwrittenLoose(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"overwrittenLooseFilesColor"_s, c);
}

QColor ColorSettings::modlistOverwritingLoose() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"overwritingLooseFilesColor"_s,
                     QColor(255, 0, 0, 64));
}

void ColorSettings::setModlistOverwritingLoose(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"overwritingLooseFilesColor"_s, c);
}

QColor ColorSettings::modlistOverwrittenArchive() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"overwrittenArchiveFilesColor"_s,
                     QColor(0, 255, 255, 64));
}

void ColorSettings::setModlistOverwrittenArchive(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"overwrittenArchiveFilesColor"_s, c);
}

QColor ColorSettings::modlistOverwritingArchive() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"overwritingArchiveFilesColor"_s,
                     QColor(255, 0, 255, 64));
}

void ColorSettings::setModlistOverwritingArchive(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"overwritingArchiveFilesColor"_s, c);
}

QColor ColorSettings::modlistContainsFile() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"containsFileColor"_s,
                     QColor(0, 0, 255, 64));
}

void ColorSettings::setModlistContainsFile(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"containsFileColor"_s, c);
}

QColor ColorSettings::pluginListContained() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"containedColor"_s,
                     QColor(0, 0, 255, 64));
}

void ColorSettings::setPluginListContained(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"containedColor"_s, c);
}

QColor ColorSettings::pluginListMaster() const
{
  return get<QColor>(m_Settings, u"Settings"_s, u"masterColor"_s,
                     QColor(255, 255, 0, 64));
}

void ColorSettings::setPluginListMaster(const QColor& c)
{
  set(m_Settings, u"Settings"_s, u"masterColor"_s, c);
}

std::optional<QColor> ColorSettings::previousSeparatorColor() const
{
  const auto c =
      getOptional<QColor>(m_Settings, u"General"_s, u"previousSeparatorColor"_s);
  if (c && c->isValid()) {
    return c;
  }

  return {};
}

void ColorSettings::setPreviousSeparatorColor(const QColor& c) const
{
  set(m_Settings, u"General"_s, u"previousSeparatorColor"_s, c);
}

void ColorSettings::removePreviousSeparatorColor()
{
  remove(m_Settings, u"General"_s, u"previousSeparatorColor"_s);
}

bool ColorSettings::colorSeparatorScrollbar() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"colorSeparatorScrollbars"_s, true);
}

void ColorSettings::setColorSeparatorScrollbar(bool b)
{
  set(m_Settings, u"Settings"_s, u"colorSeparatorScrollbars"_s, b);
}

QColor ColorSettings::idealTextColor(const QColor& rBackgroundColor)
{
  if (rBackgroundColor.alpha() < 50)
    return QColor(Qt::black);

  // "inverse' of luminance of the background
  int iLuminance = (rBackgroundColor.red() * 0.299) +
                   (rBackgroundColor.green() * 0.587) +
                   (rBackgroundColor.blue() * 0.114);
  return QColor(iLuminance >= 128 ? Qt::black : Qt::white);
}

PluginSettings::PluginSettings(QSettings& settings) : m_Settings(settings) {}

void PluginSettings::clearPlugins()
{
  m_Plugins.clear();
  m_PluginSettings.clear();
  m_PluginBlacklist.clear();

  m_PluginBlacklist = readBlacklist();
}

void PluginSettings::registerPlugin(IPlugin* plugin)
{
  m_Plugins.push_back(plugin);
  m_PluginSettings.insert(plugin->name(), QVariantMap());
  m_PluginDescriptions.insert(plugin->name(), QVariantMap());

  for (const PluginSetting& setting : plugin->settings()) {
    const QString settingName = plugin->name() % u"/"_s % setting.key;

    QVariant temp = get<QVariant>(m_Settings, u"Plugins"_s, settingName, QVariant());

    // No previous enabled? Skip.
    if (setting.key == "enabled" && (!temp.isValid() || !temp.canConvert<bool>())) {
      continue;
    }

    if (!temp.isValid()) {
      temp = setting.defaultValue;
    } else if (!temp.convert(setting.defaultValue.metaType())) {
      log::warn("failed to interpret \"{}\" as correct type for \"{}\" in plugin "
                "\"{}\", using default",
                temp.toString(), setting.key, plugin->name());

      temp = setting.defaultValue;
    }

    m_PluginSettings[plugin->name()][setting.key] = temp;

    m_PluginDescriptions[plugin->name()][setting.key] =
        QStringLiteral("%1 (default: %2)")
            .arg(setting.description, setting.defaultValue.toString());
  }

  // Handle previous "enabled" settings:
  if (m_PluginSettings[plugin->name()].contains(u"enabled"_s)) {
    setPersistent(plugin->name(), u"enabled"_s,
                  m_PluginSettings[plugin->name()][u"enabled"_s].toBool(), true);
    m_PluginSettings[plugin->name()].remove(u"enabled"_s);
    m_PluginDescriptions[plugin->name()].remove(u"enabled"_s);

    // We need to drop it manually in Settings since it is not possible to remove plugin
    // settings:
    remove(m_Settings, u"Plugins"_s, plugin->name() % u"/enabled"_s);
  }
}

void PluginSettings::unregisterPlugin(IPlugin* plugin)
{
  auto it = std::find(m_Plugins.begin(), m_Plugins.end(), plugin);
  if (it != m_Plugins.end()) {
    m_Plugins.erase(it);
  }
  m_PluginSettings.remove(plugin->name());
  m_PluginDescriptions.remove(plugin->name());
}

std::vector<MOBase::IPlugin*> PluginSettings::plugins() const
{
  return m_Plugins;
}

QVariant PluginSettings::setting(const QString& pluginName, const QString& key) const
{
  auto iterPlugin = m_PluginSettings.find(pluginName);
  if (iterPlugin == m_PluginSettings.end()) {
    return QVariant();
  }

  auto iterSetting = iterPlugin->find(key);
  if (iterSetting == iterPlugin->end()) {
    return QVariant();
  }

  return *iterSetting;
}

void PluginSettings::setSetting(const QString& pluginName, const QString& key,
                                const QVariant& value)
{
  auto iterPlugin = m_PluginSettings.find(pluginName);

  if (iterPlugin == m_PluginSettings.end()) {
    throw MyException(QObject::tr("attempt to store setting for unknown plugin \"%1\"")
                          .arg(pluginName));
  }

  QVariant oldValue = m_PluginSettings[pluginName][key];

  // store the new setting both in memory and in the ini
  m_PluginSettings[pluginName][key] = value;
  set(m_Settings, u"Plugins"_s, pluginName % u"/"_s % key, value);

  // emit signal:
  emit pluginSettingChanged(pluginName, key, oldValue, value);
}

QVariantMap PluginSettings::settings(const QString& pluginName) const
{
  return m_PluginSettings[pluginName];
}

void PluginSettings::setSettings(const QString& pluginName, const QVariantMap& map)
{
  auto iterPlugin = m_PluginSettings.find(pluginName);

  if (iterPlugin == m_PluginSettings.end()) {
    throw MyException(QObject::tr("attempt to store setting for unknown plugin \"%1\"")
                          .arg(pluginName));
  }

  QVariantMap oldSettings      = m_PluginSettings[pluginName];
  m_PluginSettings[pluginName] = map;

  // Emit signals for settings that have been changed or added:
  for (auto& k : map.keys()) {
    // .value() return a default-constructed QVariant if k is not in oldSettings:
    QVariant oldValue = oldSettings.value(k);
    if (oldValue != map[k]) {
      emit pluginSettingChanged(pluginName, k, oldSettings.value(k), map[k]);
    }
  }

  // Emit signals for settings that have been removed:
  for (auto& k : oldSettings.keys()) {
    if (!map.contains(k)) {
      emit pluginSettingChanged(pluginName, k, oldSettings[k], QVariant());
    }
  }
}

QVariantMap PluginSettings::descriptions(const QString& pluginName) const
{
  return m_PluginDescriptions[pluginName];
}

void PluginSettings::setDescriptions(const QString& pluginName, const QVariantMap& map)
{
  m_PluginDescriptions[pluginName] = map;
}

QVariant PluginSettings::persistent(const QString& pluginName, const QString& key,
                                    const QVariant& def) const
{
  if (!m_PluginSettings.contains(pluginName)) {
    return def;
  }

  return get<QVariant>(m_Settings, u"PluginPersistance"_s, pluginName % u"/"_s % key,
                       def);
}

void PluginSettings::setPersistent(const QString& pluginName, const QString& key,
                                   const QVariant& value, bool sync)
{
  if (!m_PluginSettings.contains(pluginName)) {
    throw MyException(QObject::tr("attempt to store setting for unknown plugin \"%1\"")
                          .arg(pluginName));
  }

  set(m_Settings, u"PluginPersistance"_s, pluginName % u"/"_s % key, value);

  if (sync) {
    m_Settings.sync();
  }
}

void PluginSettings::addBlacklist(const QString& fileName)
{
  m_PluginBlacklist.insert(fileName);
  writeBlacklist();
}

bool PluginSettings::blacklisted(const QString& fileName) const
{
  return m_PluginBlacklist.contains(fileName);
}

void PluginSettings::setBlacklist(const QStringList& pluginNames)
{
  m_PluginBlacklist.clear();

  for (const auto& name : pluginNames) {
    m_PluginBlacklist.insert(name);
  }
}

const QSet<QString>& PluginSettings::blacklist() const
{
  return m_PluginBlacklist;
}

void PluginSettings::save()
{
  for (auto iterPlugins = m_PluginSettings.begin();
       iterPlugins != m_PluginSettings.end(); ++iterPlugins) {
    for (auto iterSettings = iterPlugins->begin(); iterSettings != iterPlugins->end();
         ++iterSettings) {
      const auto key = iterPlugins.key() % u"/"_s % iterSettings.key();
      set(m_Settings, u"Plugins"_s, key, iterSettings.value());
    }
  }

  writeBlacklist();
}

void PluginSettings::writeBlacklist()
{
  const auto current = readBlacklist();

  if (current.size() > m_PluginBlacklist.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, u"pluginBlacklist"_s);
  }

  ScopedWriteArray swa(m_Settings, u"pluginBlacklist"_s, m_PluginBlacklist.size());

  for (const QString& plugin : m_PluginBlacklist) {
    swa.next();
    swa.set(u"name"_s, plugin);
  }
}

QSet<QString> PluginSettings::readBlacklist() const
{
  QSet<QString> set;

  ScopedReadArray sra(m_Settings, u"pluginBlacklist"_s);
  sra.for_each([&] {
    set.insert(sra.get<QString>(u"name"_s));
  });

  return set;
}

const QString PathSettings::BaseDirVariable = u"%BASE_DIR%"_s;

PathSettings::PathSettings(QSettings& settings) : m_Settings(settings) {}

std::map<QString, QString> PathSettings::recent() const
{
  std::map<QString, QString> map;

  ScopedReadArray sra(m_Settings, u"recentDirectories"_s);

  sra.for_each([&] {
    const QVariant name = sra.get<QVariant>(u"name"_s);
    const QVariant dir  = sra.get<QVariant>(u"directory"_s);

    if (name.isValid() && dir.isValid()) {
      map.emplace(name.toString(), dir.toString());
    }
  });

  return map;
}

void PathSettings::setRecent(const std::map<QString, QString>& map)
{
  const auto current = recent();

  if (current.size() > map.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, u"recentDirectories"_s);
  }

  ScopedWriteArray swa(m_Settings, u"recentDirectories"_s, map.size());

  for (auto&& p : map) {
    swa.next();

    swa.set(u"name"_s, p.first);
    swa.set(u"directory"_s, p.second);
  }
}

QString PathSettings::getConfigurablePath(const QString& key, const QString& def,
                                          bool resolve) const
{
  QString result = QDir::fromNativeSeparators(
      get<QString>(m_Settings, u"Settings"_s, key, makeDefaultPath(def)));

  if (resolve) {
    result = PathSettings::resolve(result, base());
  }

  return result;
}

void PathSettings::setConfigurablePath(const QString& key, const QString& path)
{
  if (path.isEmpty()) {
    remove(m_Settings, u"Settings"_s, key);
  } else {
    set(m_Settings, u"Settings"_s, key, path);
  }
}

QString PathSettings::resolve(const QString& path, const QString& baseDir)
{
  QString s = path;
  s.replace(BaseDirVariable, baseDir);
  return s;
}

QString PathSettings::makeDefaultPath(const QString dirName)
{
  return BaseDirVariable % u"/"_s % dirName;
}

QString PathSettings::base() const
{
  const QString dataPath = QFileInfo(m_Settings.fileName()).dir().path();

  return QDir::fromNativeSeparators(
      get<QString>(m_Settings, u"Settings"_s, u"base_directory"_s, dataPath));
}

QString PathSettings::downloads(bool resolve) const
{
  return getConfigurablePath(u"download_directory"_s, AppConfig::downloadPath(),
                             resolve);
}

QString PathSettings::cache(bool resolve) const
{
  return getConfigurablePath(u"cache_directory"_s, AppConfig::cachePath(), resolve);
}

QString PathSettings::mods(bool resolve) const
{
  return getConfigurablePath(u"mod_directory"_s, AppConfig::modsPath(), resolve);
}

QString PathSettings::profiles(bool resolve) const
{
  return getConfigurablePath(u"profiles_directory"_s, AppConfig::profilesPath(),
                             resolve);
}

QString PathSettings::overwrite(bool resolve) const
{
  return getConfigurablePath(u"overwrite_directory"_s, AppConfig::overwritePath(),
                             resolve);
}

void PathSettings::setBase(const QString& path)
{
  if (path.isEmpty()) {
    remove(m_Settings, u"Settings"_s, u"base_directory"_s);
  } else {
    set(m_Settings, u"Settings"_s, u"base_directory"_s, path);
  }
}

void PathSettings::setDownloads(const QString& path)
{
  setConfigurablePath(u"download_directory"_s, path);
}

void PathSettings::setMods(const QString& path)
{
  setConfigurablePath(u"mod_directory"_s, path);
}

void PathSettings::setCache(const QString& path)
{
  setConfigurablePath(u"cache_directory"_s, path);
}

void PathSettings::setProfiles(const QString& path)
{
  setConfigurablePath(u"profiles_directory"_s, path);
}

void PathSettings::setOverwrite(const QString& path)
{
  setConfigurablePath(u"overwrite_directory"_s, path);
}

NetworkSettings::NetworkSettings(QSettings& settings, bool globalInstance)
    : m_Settings(settings)
{
  if (globalInstance) {
    updateCustomBrowser();
  }
}

void NetworkSettings::updateCustomBrowser()
{
  if (useCustomBrowser()) {
    MOBase::shell::SetUrlHandler(customBrowserCommand());
  } else {
    MOBase::shell::SetUrlHandler("");
  }
}

bool NetworkSettings::offlineMode() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"offline_mode"_s, false);
}

void NetworkSettings::setOfflineMode(bool b)
{
  set(m_Settings, u"Settings"_s, u"offline_mode"_s, b);
}

bool NetworkSettings::useProxy() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"use_proxy"_s, false);
}

void NetworkSettings::setUseProxy(bool b)
{
  set(m_Settings, u"Settings"_s, u"use_proxy"_s, b);
}

void NetworkSettings::setDownloadSpeed(const QString& name, int bytesPerSecond)
{
  auto current = servers();

  for (auto& server : current) {
    if (server.name() == name) {
      server.addDownload(bytesPerSecond);
      updateServers(current);
      return;
    }
  }

  log::error("server '{}' not found while trying to add a download with bps {}", name,
             bytesPerSecond);
}

ServerList NetworkSettings::servers() const
{
  ServerList list;

  {
    ScopedReadArray sra(m_Settings, u"Servers"_s);

    sra.for_each([&] {
      ServerInfo::SpeedList lastDownloads;

      const auto lastDownloadsString = sra.get<QString>(u"lastDownloads"_s, "");

      for (const auto& s : lastDownloadsString.split(' ')) {
        const auto bytesPerSecond = s.toInt();
        if (bytesPerSecond > 0) {
          lastDownloads.push_back(bytesPerSecond);
        }
      }

      ServerInfo server(
          sra.get<QString>(u"name"_s, ""), sra.get<bool>(u"premium"_s, false),
          QDate::fromString(sra.get<QString>(u"lastSeen"_s, ""), Qt::ISODate),
          sra.get<int>(u"preferred"_s, 0), lastDownloads);

      list.add(std::move(server));
    });
  }

  return list;
}

void NetworkSettings::updateServers(ServerList newServers)
{
  // clean up unavailable servers
  newServers.cleanup();

  const auto current = servers();

  if (current.size() > newServers.size()) {
    // Qt can't remove array elements, the section must be cleared
    removeSection(m_Settings, u"Servers"_s);
  }

  ScopedWriteArray swa(m_Settings, u"Servers"_s, newServers.size());

  for (const auto& server : newServers) {
    swa.next();

    swa.set(u"name"_s, server.name());
    swa.set(u"premium"_s, server.isPremium());
    swa.set(u"lastSeen"_s, server.lastSeen().toString(Qt::ISODate));
    swa.set(u"preferred"_s, server.preferred());

    QString lastDownloads;
    for (const auto& speed : server.lastDownloads()) {
      if (speed > 0) {
        lastDownloads += QStringLiteral("%1 ").arg(speed);
      }
    }

    swa.set(u"lastDownloads"_s, lastDownloads.trimmed());
  }
}

void NetworkSettings::updateFromOldMap()
{
  // servers used to be a map of byte arrays until 2.2.1, it's now an array of
  // individual values instead
  //
  // so post 2.2.1, only one key is returned: "size", the size of the arrays;
  // in 2.2.1, one key per server is returned

  // sanity check that this is really 2.2.1
  {
    const QStringList keys = ScopedGroup(m_Settings, u"Servers"_s).keys();

    for (auto&& k : keys) {
      if (k == "size"_L1) {
        // this looks like an array, so the upgrade was probably already done
        return;
      }
    }
  }

  const auto servers = serversFromOldMap();
  removeSection(m_Settings, u"Servers"_s);
  updateServers(servers);
}

bool NetworkSettings::useCustomBrowser() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"use_custom_browser"_s, false);
}

void NetworkSettings::setUseCustomBrowser(bool b)
{
  set(m_Settings, u"Settings"_s, u"use_custom_browser"_s, b);
  updateCustomBrowser();
}

QString NetworkSettings::customBrowserCommand() const
{
  return get<QString>(m_Settings, u"Settings"_s, u"custom_browser"_s, u""_s);
}

void NetworkSettings::setCustomBrowserCommand(const QString& s)
{
  set(m_Settings, u"Settings"_s, u"custom_browser"_s, s);
  updateCustomBrowser();
}

ServerList NetworkSettings::serversFromOldMap() const
{
  // for 2.2.1 and before

  ServerList list;
  const ScopedGroup sg(m_Settings, u"Servers"_s);

  sg.for_each([&](auto&& serverKey) {
    QVariantMap data = sg.get<QVariantMap>(serverKey);

    ServerInfo server(serverKey, data[u"premium"_s].toBool(),
                      data[u"lastSeen"_s].toDate(), data[u"preferred"_s].toInt(), {});

    // ignoring download count and speed, it's now a list of values instead of
    // a total

    list.add(std::move(server));
  });

  return list;
}

void NetworkSettings::dump() const
{
  log::debug("servers:");

  for (const auto& server : servers()) {
    QString lastDownloads;
    for (auto speed : server.lastDownloads()) {
      lastDownloads += QStringLiteral("%1 ").arg(speed);
    }

    log::debug("  . {} premium={} lastSeen={} preferred={} lastDownloads={}",
               server.name(), server.isPremium() ? "yes" : "no",
               server.lastSeen().toString(Qt::ISODate), server.preferred(),
               lastDownloads.trimmed());
  }
}

NexusSettings::NexusSettings(Settings& parent, QSettings& settings)
    : m_Parent(parent), m_Settings(settings)
{}

bool NexusSettings::endorsementIntegration() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"endorsement_integration"_s, true);
}

void NexusSettings::setEndorsementIntegration(bool b) const
{
  set(m_Settings, u"Settings"_s, u"endorsement_integration"_s, b);
}

EndorsementState NexusSettings::endorsementState() const
{
  return endorsementStateFromString(
      get<QString>(m_Settings, u"General"_s, u"endorse_state"_s, ""));
}

void NexusSettings::setEndorsementState(EndorsementState s)
{
  const auto v = toString(s);

  if (v.isEmpty()) {
    remove(m_Settings, u"General"_s, u"endorse_state"_s);
  } else {
    set(m_Settings, u"General"_s, u"endorse_state"_s, v);
  }
}

bool NexusSettings::trackedIntegration() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"tracked_integration"_s, true);
}

void NexusSettings::setTrackedIntegration(bool b) const
{
  set(m_Settings, u"Settings"_s, u"tracked_integration"_s, b);
}

bool NexusSettings::categoryMappings() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"category_mappings"_s, true);
}

void NexusSettings::setCategoryMappings(bool b) const
{
  set(m_Settings, u"Settings"_s, u"category_mappings"_s, b);
}

void NexusSettings::registerAsNXMHandler(bool force)
{
  const auto nxmPath =
      QCoreApplication::applicationDirPath() % u"/"_s % AppConfig::nxmHandlerExe();

  const auto executable = QCoreApplication::applicationFilePath();

  QStringList parameters;

  QString mode = force ? u"forcereg"_s : u"reg"_s;

  parameters << mode;
  QString game = m_Parent.game().plugin()->gameShortName();
  for (const QString& altGame : m_Parent.game().plugin()->validShortNames()) {
    game += u","_s + altGame;
  }
  parameters << game;
  parameters << executable;

  log::debug("running nxmhandler with arguments: {}", parameters.join(' '));

  QProcess p;
  p.setProgram(nxmPath);
  p.setArguments(parameters);

  auto result = p.startDetached();

  if (!result) {
    QMessageBox::critical(
        nullptr, QObject::tr("Failed"),
        QObject::tr("Failed to start the helper application: %1").arg(p.errorString()));
  }
}

std::vector<std::chrono::seconds> NexusSettings::validationTimeouts() const
{
  using namespace std::chrono_literals;

  const auto s = get<QString>(m_Settings, u"Settings"_s, u"validation_timeouts"_s, "");

  const auto numbers = s.split(' ');
  std::vector<std::chrono::seconds> v;

  for (auto ns : numbers) {
    ns = ns.trimmed();
    if (ns.isEmpty())
      continue;

    bool ok      = false;
    const auto n = ns.toInt(&ok);

    if (!ok || n < 0 || n > 100) {
      log::error("bad validation_timeouts number '{}'", ns);
      continue;
    }

    v.push_back(std::chrono::seconds(n));
  }

  if (v.empty())
    v = {10s, 15s, 20s};

  return v;
}

SteamSettings::SteamSettings(Settings& parent, QSettings& settings)
    : m_Parent(parent), m_Settings(settings)
{}

QString SteamSettings::appID() const
{
  return get<QString>(m_Settings, u"Settings"_s, u"app_id"_s,
                      m_Parent.game().plugin()->steamAPPId());
}

void SteamSettings::setAppID(const QString& id)
{
  if (id.isEmpty()) {
    remove(m_Settings, u"Settings"_s, u"app_id"_s);
  } else {
    set(m_Settings, u"Settings"_s, u"app_id"_s, id);
  }
}

InterfaceSettings::InterfaceSettings(QSettings& settings) : m_Settings(settings) {}

bool InterfaceSettings::lockGUI() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"lock_gui"_s, true);
}

void InterfaceSettings::setLockGUI(bool b)
{
  set(m_Settings, u"Settings"_s, u"lock_gui"_s, b);
}

std::optional<QString> InterfaceSettings::styleName() const
{
  return getOptional<QString>(m_Settings, u"Settings"_s, u"style"_s);
}

void InterfaceSettings::setStyleName(const QString& name)
{
  set(m_Settings, u"Settings"_s, u"style"_s, name);
}

bool InterfaceSettings::collapsibleSeparators(Qt::SortOrder order) const
{
  return get<bool>(m_Settings, u"Settings"_s,
                   order == Qt::AscendingOrder ? u"collapsible_separators_asc"_s
                                               : u"collapsible_separators_dsc"_s,
                   true);
}

void InterfaceSettings::setCollapsibleSeparators(bool ascending, bool descending)
{
  set(m_Settings, u"Settings"_s, u"collapsible_separators_asc"_s, ascending);
  set(m_Settings, u"Settings"_s, u"collapsible_separators_dsc"_s, descending);
}

bool InterfaceSettings::collapsibleSeparatorsHighlightTo() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"collapsible_separators_conflicts_to"_s,
                   true);
}

void InterfaceSettings::setCollapsibleSeparatorsHighlightTo(bool b)
{
  set(m_Settings, u"Settings"_s, u"collapsible_separators_conflicts_to"_s, b);
}

bool InterfaceSettings::collapsibleSeparatorsHighlightFrom() const
{
  return get<bool>(m_Settings, u"Settings"_s,
                   u"collapsible_separators_conflicts_from"_s, true);
}

void InterfaceSettings::setCollapsibleSeparatorsHighlightFrom(bool b)
{
  set(m_Settings, u"Settings"_s, u"collapsible_separators_conflicts_from"_s, b);
}

bool InterfaceSettings::collapsibleSeparatorsIcons(int column) const
{
  return get<bool>(m_Settings, u"Settings"_s,
                   QStringLiteral("collapsible_separators_icons_%1").arg(column), true);
}

void InterfaceSettings::setCollapsibleSeparatorsIcons(int column, bool show)
{
  set(m_Settings, u"Settings"_s,
      QStringLiteral("collapsible_separators_icons_%1").arg(column), show);
}

bool InterfaceSettings::collapsibleSeparatorsPerProfile() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"collapsible_separators_per_profile"_s,
                   false);
}

void InterfaceSettings::setCollapsibleSeparatorsPerProfile(bool b)
{
  set(m_Settings, u"Settings"_s, u"collapsible_separators_per_profile"_s, b);
}

bool InterfaceSettings::saveFilters() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"save_filters"_s, false);
}

void InterfaceSettings::setSaveFilters(bool b)
{
  set(m_Settings, u"Settings"_s, u"save_filters"_s, b);
}

bool InterfaceSettings::autoCollapseOnHover() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"auto_collapse_on_hover"_s, false);
}

void InterfaceSettings::setAutoCollapseOnHover(bool b)
{
  set(m_Settings, u"Settings"_s, u"auto_collapse_on_hover"_s, b);
}

bool InterfaceSettings::checkUpdateAfterInstallation() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"autocheck_update_install"_s, true);
}

void InterfaceSettings::setCheckUpdateAfterInstallation(bool b)
{
  set(m_Settings, u"Settings"_s, u"autocheck_update_install"_s, b);
}

bool InterfaceSettings::compactDownloads() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"compact_downloads"_s, false);
}

void InterfaceSettings::setCompactDownloads(bool b)
{
  set(m_Settings, u"Settings"_s, u"compact_downloads"_s, b);
}

bool InterfaceSettings::metaDownloads() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"meta_downloads"_s, false);
}

void InterfaceSettings::setMetaDownloads(bool b)
{
  set(m_Settings, u"Settings"_s, u"meta_downloads"_s, b);
}

bool InterfaceSettings::hideDownloadsAfterInstallation() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"autohide_downloads"_s, false);
}

void InterfaceSettings::setHideDownloadsAfterInstallation(bool b)
{
  set(m_Settings, u"Settings"_s, u"autohide_downloads"_s, b);
}

bool InterfaceSettings::hideAPICounter() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"hide_api_counter"_s, false);
}

void InterfaceSettings::setHideAPICounter(bool b)
{
  set(m_Settings, u"Settings"_s, u"hide_api_counter"_s, b);
}

bool InterfaceSettings::displayForeign() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"display_foreign"_s, true);
}

void InterfaceSettings::setDisplayForeign(bool b)
{
  set(m_Settings, u"Settings"_s, u"display_foreign"_s, b);
}

QString InterfaceSettings::language()
{
  QString result = get<QString>(m_Settings, u"Settings"_s, u"language"_s, "");

  if (result.isEmpty()) {
    QStringList languagePreferences = QLocale::system().uiLanguages();

    if (languagePreferences.length() > 0) {
      // the users most favoritest language
      result = languagePreferences.at(0);
    } else {
      // fallback system locale
      result = QLocale::system().name();
    }
  }

  return result;
}

void InterfaceSettings::setLanguage(const QString& name)
{
  set(m_Settings, u"Settings"_s, u"language"_s, name);
}

bool InterfaceSettings::isTutorialCompleted(const QString& windowName) const
{
  return get<bool>(m_Settings, u"CompletedWindowTutorials"_s, windowName, false);
}

void InterfaceSettings::setTutorialCompleted(const QString& windowName, bool b)
{
  set(m_Settings, u"CompletedWindowTutorials"_s, windowName, b);
}

bool InterfaceSettings::showChangeGameConfirmation() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"show_change_game_confirmation"_s, true);
}

void InterfaceSettings::setShowChangeGameConfirmation(bool b)
{
  set(m_Settings, u"Settings"_s, u"show_change_game_confirmation"_s, b);
}

bool InterfaceSettings::showMenubarOnAlt() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"show_menubar_on_alt"_s, true);
}

void InterfaceSettings::setShowMenubarOnAlt(bool b)
{
  set(m_Settings, u"Settings"_s, u"show_menubar_on_alt"_s, b);
}

bool InterfaceSettings::doubleClicksOpenPreviews() const
{
  return get<bool>(m_Settings, u"Settings"_s, u"double_click_previews"_s, true);
}

void InterfaceSettings::setDoubleClicksOpenPreviews(bool b)
{
  set(m_Settings, u"Settings"_s, u"double_click_previews"_s, b);
}

FilterWidget::Options InterfaceSettings::filterOptions() const
{
  FilterWidget::Options o;

  o.useRegex = get<bool>(m_Settings, u"Settings"_s, u"filter_regex"_s, false);
  o.regexCaseSensitive =
      get<bool>(m_Settings, u"Settings"_s, u"regex_case_sensitive"_s, false);
  o.regexExtended = get<bool>(m_Settings, u"Settings"_s, u"regex_extended"_s, false);
  o.scrollToSelection =
      get<bool>(m_Settings, u"Settings"_s, u"filter_scroll_to_selection"_s, false);

  return o;
}

void InterfaceSettings::setFilterOptions(const FilterWidget::Options& o)
{
  set(m_Settings, u"Settings"_s, u"filter_regex"_s, o.useRegex);
  set(m_Settings, u"Settings"_s, u"regex_case_sensitive"_s, o.regexCaseSensitive);
  set(m_Settings, u"Settings"_s, u"regex_extended"_s, o.regexExtended);
  set(m_Settings, u"Settings"_s, u"filter_scroll_to_selection"_s, o.scrollToSelection);
}

DiagnosticsSettings::DiagnosticsSettings(QSettings& settings) : m_Settings(settings) {}

log::Levels DiagnosticsSettings::logLevel() const
{
  return get<log::Levels>(m_Settings, u"Settings"_s, u"log_level"_s, log::Levels::Info);
}

void DiagnosticsSettings::setLogLevel(log::Levels level)
{
  set(m_Settings, u"Settings"_s, u"log_level"_s, level);
}

lootcli::LogLevels DiagnosticsSettings::lootLogLevel() const
{
  return get<lootcli::LogLevels>(m_Settings, u"Settings"_s, u"loot_log_level"_s,
                                 lootcli::LogLevels::Info);
}

void DiagnosticsSettings::setLootLogLevel(lootcli::LogLevels level)
{
  set(m_Settings, u"Settings"_s, u"loot_log_level"_s, level);
}

env::CoreDumpTypes DiagnosticsSettings::coreDumpType() const
{
  return get<env::CoreDumpTypes>(m_Settings, u"Settings"_s, u"crash_dumps_type"_s,
                                 env::CoreDumpTypes::Mini);
}

void DiagnosticsSettings::setCoreDumpType(env::CoreDumpTypes type)
{
  set(m_Settings, u"Settings"_s, u"crash_dumps_type"_s, type);
}

int DiagnosticsSettings::maxCoreDumps() const
{
  return get<int>(m_Settings, u"Settings"_s, u"crash_dumps_max"_s, 5);
}

void DiagnosticsSettings::setMaxCoreDumps(int n)
{
  set(m_Settings, u"Settings"_s, u"crash_dumps_max"_s, n);
}

std::chrono::seconds DiagnosticsSettings::spawnDelay() const
{
  return std::chrono::seconds(get<int>(m_Settings, u"Settings"_s, u"spawn_delay"_s, 0));
}

void DiagnosticsSettings::setSpawnDelay(std::chrono::seconds t)
{
  set(m_Settings, u"Settings"_s, u"spawn_delay"_s, QVariant::fromValue(t.count()));
}

void GlobalSettings::updateRegistryKey()
{
  const QString OldOrganization  = u"Tannin"_s;
  const QString OldApplication   = u"Mod Organizer"_s;
  const QString OldInstanceValue = u"CurrentInstance"_s;

  const QString OldRootKey = u"Software\\"_s % OldOrganization;

  if (env::registryValueExists(OldRootKey % u"\\"_s % OldApplication,
                               OldInstanceValue)) {
    QSettings old(OldOrganization, OldApplication);
    setCurrentInstance(old.value(OldInstanceValue).toString());
    old.remove(OldInstanceValue);
  }

  env::deleteRegistryKeyIfEmpty(OldRootKey);
}

QString GlobalSettings::currentInstance()
{
  return settings().value(u"CurrentInstance"_s, "").toString();
}

void GlobalSettings::setCurrentInstance(const QString& s)
{
  settings().setValue(u"CurrentInstance"_s, s);
}

QSettings GlobalSettings::settings()
{
  const QString Organization = u"Mod Organizer Team"_s;
  const QString Application  = u"Mod Organizer"_s;

  return QSettings(Organization, Application);
}

bool GlobalSettings::hideCreateInstanceIntro()
{
  return settings().value(u"HideCreateInstanceIntro"_s, false).toBool();
}

void GlobalSettings::setHideCreateInstanceIntro(bool b)
{
  settings().setValue(u"HideCreateInstanceIntro"_s, b);
}

bool GlobalSettings::hideTutorialQuestion()
{
  return settings().value(u"HideTutorialQuestion"_s, false).toBool();
}

void GlobalSettings::setHideTutorialQuestion(bool b)
{
  settings().setValue(u"HideTutorialQuestion"_s, b);
}

bool GlobalSettings::hideCategoryReminder()
{
  return settings().value(u"HideCategoryReminder"_s, false).toBool();
}

void GlobalSettings::setHideCategoryReminder(bool b)
{
  settings().setValue(u"HideCategoryReminder"_s, b);
}

bool GlobalSettings::hideAssignCategoriesQuestion()
{
  return settings().value(u"HideAssignCategoriesQuestion"_s, false).toBool();
}

void GlobalSettings::setHideAssignCategoriesQuestion(bool b)
{
  settings().setValue(u"HideAssignCategoriesQuestion"_s, b);
}

bool GlobalSettings::clearNexusApiKey()
{
  return setNexusApiKey("");
}

void GlobalSettings::resetDialogs()
{
  setHideCreateInstanceIntro(false);
  setHideTutorialQuestion(false);
}
