/*
Copyright (C) 2015 Sebastian Herbord. All rights reserved.

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

#include "overlayfsconnector.h"
#include "organizercore.h"
#include "overlayfs/overlayfsmanager.h"
#include "settings.h"
#include "shared/util.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QProgressDialog>
#include <iomanip>
#include <memory>
#include <sstream>

static constexpr char SHMID[] = "mod_organizer_instance";
using namespace MOBase;
using namespace Qt::StringLiterals;

std::string to_hex(void* bufferIn, size_t bufferSize)
{
  unsigned char* buffer = static_cast<unsigned char*>(bufferIn);
  std::ostringstream temp;
  temp << std::hex;
  for (size_t i = 0; i < bufferSize; ++i) {
    temp << std::setfill('0') << std::setw(2) << (unsigned int)buffer[i];
    if ((i % 16) == 15) {
      temp << "\n";
    } else {
      temp << " ";
    }
  }
  return temp.str();
}

LogLevel toOverlayfsLogLevel(log::Levels level)
{
  switch (level) {
  case log::Info:
    return LogLevel::Info;
  case log::Warning:
    return LogLevel::Warning;
  case log::Error:
    return LogLevel::Error;
  case log::Debug:
  default:
    return LogLevel::Debug;
  }
}

OverlayfsConnector::OverlayfsConnector()
    : m_overlayfsManager(OverlayFsManager::getInstance(
          (qApp->property("dataPath").toString() +
           QStringLiteral("/logs/overlayfs-%1.log")
               .arg(QDateTime::currentDateTimeUtc().toString(u"yyyy-MM-dd_hh-mm-ss"_s)))
              .toStdString()))
{
  const auto& s = Settings::instance();

  const LogLevel logLevel = toOverlayfsLogLevel(s.diagnostics().logLevel());

  m_overlayfsManager.setLogLevel(logLevel);

  log::debug("initializing overlayfs:\n"
             " . instance: {}\n"
             " . log: {}",
             SHMID, OverlayFsManager::logLevelToString(logLevel));

  for (auto& suffix : s.skipFileSuffixes()) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_overlayfsManager.addSkipFileSuffix(suffix.toStdString());
  }

  for (auto& dir : s.skipDirectories()) {
    m_overlayfsManager.addSkipDirectory(dir.toStdString());
  }
}

OverlayfsConnector::~OverlayfsConnector()
{
  if (m_overlayfsManager.isMounted()) {
    m_overlayfsManager.umount();
  }
}

void OverlayfsConnector::updateMapping(const MappingType& mapping)
{
  const auto start = std::chrono::high_resolution_clock::now();

  QProgressDialog progress(qApp->activeWindow());
  progress.setLabelText(tr("Preparing Overlayfs"));
  progress.setMaximum(static_cast<int>(mapping.size()));
  progress.show();

  int value = 0;
  int files = 0;
  int dirs  = 0;

  log::debug("Updating Overlayfs mappings...");

  m_overlayfsManager.clearMappings();

  for (const auto& map : mapping) {
    if (progress.wasCanceled()) {
      m_overlayfsManager.clearMappings();
      throw OverlayfsConnectorException(u"Overlayfs mapping canceled by user"_s);
    }
    progress.setValue(value++);
    if (value % 10 == 0) {
      QCoreApplication::processEvents();
    }

    if (map.isDirectory) {
      m_overlayfsManager.addDirectory(map.source.toStdString(),
                                      map.destination.toStdString());
      ++dirs;
    } else {
      m_overlayfsManager.addFile(map.source.toStdString(),
                                 map.destination.toStdString());
      ++files;
    }
  }

  const auto end  = std::chrono::high_resolution_clock::now();
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  log::debug("Overlayfs mappings updated, linked {} dirs and {} files in {}ms", dirs,
             files, time.count());
}

void OverlayfsConnector::updateParams(MOBase::log::Levels logLevel,
                                      [[maybe_unused]] env::CoreDumpTypes coreDumpType,
                                      [[maybe_unused]] const QString& crashDumpsPath,
                                      [[maybe_unused]] std::chrono::seconds spawnDelay,
                                      [[maybe_unused]] QString executableBlacklist,
                                      const QStringList& skipFileSuffixes,
                                      const QStringList& skipDirectories)
{
  using namespace std::chrono;

  m_overlayfsManager.setDebugMode(false);
  m_overlayfsManager.setLogLevel(toOverlayfsLogLevel(logLevel));

  m_overlayfsManager.clearSkipFileSuffixes();
  for (auto& suffix : skipFileSuffixes) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_overlayfsManager.addSkipFileSuffix(suffix.toStdString());
  }

  m_overlayfsManager.clearSkipDirectories();
  for (auto& dir : skipDirectories) {
    m_overlayfsManager.addSkipDirectory(dir.toStdString());
  }
}

void OverlayfsConnector::updateForcedLibraries(
    const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries)
{
  m_overlayfsManager.clearLibraryForceLoads();
  for (const auto& setting : forcedLibraries) {
    if (setting.enabled()) {
      m_overlayfsManager.forceLoadLibrary(setting.process().toStdString(),
                                          setting.library().toStdString());
    }
  }
}

std::vector<HANDLE> getRunningOverlayfsProcesses()
{
  std::vector<HANDLE> pids;

  // TODO: check if this should be implemented
  // OverlayfsManager::getInstance().
  // {
  //   if (!) {
  //     log::error("failed to get usvfs process list");
  //     return {};
  //   }
  //
  // }

  return pids;
}
