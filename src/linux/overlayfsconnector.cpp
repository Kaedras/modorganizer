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
#include "envmodule.h"
#include "organizercore.h"
#include "settings.h"
#include "shared/util.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QProgressDialog>
#include <QTemporaryFile>
#include <iomanip>
#include <memory>
#include <qstandardpaths.h>
#include <sstream>
#include "overlayfs/overlayfs.h"

static constexpr char SHMID[] = "mod_organizer_instance";
using namespace MOBase;

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
//
// LogWorker::LogWorker()
//     : m_Buffer(1024, '\0'), m_QuitRequested(false),
//       m_LogFile(
//           qApp->property("dataPath").toString() +
//           QString("/logs/overlayfs-%1.log")
//               .arg(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd_hh-mm-ss")))
// {
//   m_LogFile.open(QIODevice::WriteOnly);
//   log::debug("overlayfs log messages are written to {}", m_LogFile.fileName());
// }
//
// LogWorker::~LogWorker() {}
//
// void LogWorker::process()
// {
//   MOShared::SetThisThreadName("LogWorker");
//
//   int noLogCycles = 0;
//   while (!m_QuitRequested) {
//     if (overlayfsGetLogMessages(&m_Buffer[0], m_Buffer.size(), false)) {
//       m_LogFile.write(m_Buffer.c_str());
//       m_LogFile.write("\n");
//       m_LogFile.flush();
//       noLogCycles = 0;
//     } else {
//       QThread::msleep(std::min(40, noLogCycles) * 5);
//       ++noLogCycles;
//     }
//   }
//   emit finished();
// }
//
// void LogWorker::exit()
// {
//   m_QuitRequested = true;
// }

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
    [[fallthrough]]
  default:
    return LogLevel::Debug;
  }
}
//
// CrashDumpsType toUsvfsCrashDumpsType(env::CoreDumpTypes type)
// {
//   switch (type) {
//   case env::CoreDumpTypes::None:
//     return CrashDumpsType::None;
//
//   case env::CoreDumpTypes::Data:
//     return CrashDumpsType::Data;
//
//   case env::CoreDumpTypes::Full:
//     return CrashDumpsType::Full;
//
//   case env::CoreDumpTypes::Mini:
//   default:
//     return CrashDumpsType::Mini;
//   }
// }

OverlayfsConnector::OverlayfsConnector(): m_overlayfsManager(OverlayfsManager::getInstance((qApp->property("dataPath").toString() + QString("/logs/overlayfs-%1.log").arg(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd_hh-mm-ss"))).toStdString()))
{
  using namespace std::chrono;

  const auto& s = Settings::instance();

  const LogLevel logLevel = toOverlayfsLogLevel(s.diagnostics().logLevel());
  // const auto dumpType     = toUsvfsCrashDumpsType(s.diagnostics().coreDumpType());
  // const auto delay        = duration_cast<milliseconds>(s.diagnostics().spawnDelay());
  // std::string dumpPath    = OrganizerCore::getGlobalCoreDumpPath().toStdString();

  m_overlayfsManager.setLogLevel(logLevel);
  m_overlayfsManager.setDebugMode(false);

  // usvfsInitLogging(false);

  log::debug("initializing overlayfs:\n"
             " . instance: {}\n"
             " . log: {}",
             SHMID, OverlayfsManager::logLevelToString(logLevel));

  for (auto exec : s.executablesBlacklist().split(";")) {
    m_overlayfsManager.blacklistExecutable(exec.toStdString());
  }

  for (auto& suffix : s.skipFileSuffixes()) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_overlayfsManager.addSkipFileSuffix(suffix.toStdString());
  }

  for (auto& dir : s.skipDirectories()) {
    m_overlayfsManager.addSkipDirectory(dir.toStdString());
  }

  // m_LogWorker.moveToThread(&m_WorkerThread);

  // connect(&m_WorkerThread, SIGNAL(started()), &m_LogWorker, SLOT(process()));
  // connect(&m_LogWorker, SIGNAL(finished()), &m_WorkerThread, SLOT(quit()));

  // m_WorkerThread.start(QThread::LowestPriority);

}

OverlayfsConnector::~OverlayfsConnector()
{
  m_overlayfsManager.umount();
  // m_LogWorker.exit();
  // m_WorkerThread.quit();
  // m_WorkerThread.wait();
}

void OverlayfsConnector::updateMapping(const MappingType& mapping)
{
  const auto start = std::chrono::high_resolution_clock::now();

  QProgressDialog progress(qApp->activeWindow());
  progress.setLabelText(tr("Preparing vfs"));
  progress.setMaximum(static_cast<int>(mapping.size()));
  progress.show();

  int value = 0;
  int files = 0;
  int dirs  = 0;

  log::debug("Updating Overlayfs mappings...");

  m_overlayfsManager.clearDirectories();

  // TODO: implement missing functionality
  for (const auto& map:mapping) {
    if (map.destination != mapping.front().destination) {
      throw OverlayfsConnectorException("Handling different destination paths is not yet implemented");
    }
    if (!map.isDirectory) {
      throw OverlayfsConnectorException("Handling files instead of directories is not yet implemented");
    }
  }

  for (auto map : mapping) {
    if (progress.wasCanceled()) {
      m_overlayfsManager.clearDirectories();
      throw OverlayfsConnectorException("Overlayfs mapping canceled by user");
    }
    progress.setValue(value++);
    if (value % 10 == 0) {
      QCoreApplication::processEvents();
    }

    if (map.isDirectory) {
      m_overlayfsManager.addDirectory(map.source.toStdString());
      ++dirs;
    } else {
      // usvfsVirtualLinkFile(map.source.toStdWString().c_str(),
      //                      map.destination.toStdWString().c_str(), 0);
      // ++files;
    }
  }

  const auto end  = std::chrono::high_resolution_clock::now();
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  log::debug("Overlayfs mappings updated, linked {} dirs and {} files in {}ms", dirs, files,
             time.count());
}

void OverlayfsConnector::updateParams(MOBase::log::Levels logLevel,
                                  env::CoreDumpTypes coreDumpType,
                                  const QString& crashDumpsPath,
                                  std::chrono::seconds spawnDelay,
                                  QString executableBlacklist,
                                  const QStringList& skipFileSuffixes,
                                  const QStringList& skipDirectories)
{
  using namespace std::chrono;

  m_overlayfsManager.setDebugMode(false);
  m_overlayfsManager.setLogLevel(toOverlayfsLogLevel(logLevel));

  m_overlayfsManager.clearExecutableBlacklist();
  for (auto exec : executableBlacklist.split(";")) {
    m_overlayfsManager.blacklistExecutable(exec.toStdString());
  }

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
  for (auto setting : forcedLibraries) {
    if (setting.enabled()) {
      m_overlayfsManager.forceLoadLibrary(setting.process().toStdString(), setting.library().toStdString());
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
