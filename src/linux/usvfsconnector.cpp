#include "usvfsconnector.h"
#include "organizercore.h"
#include "settings.h"
#include "shared/util.h"
#include <QApplication>
#include <sys/wait.h>
#include <usvfs-fuse/usvfsmanager.h>

static constexpr char SHMID[] = "mod_organizer_instance";
using namespace MOBase;
using namespace Qt::StringLiterals;

LogLevel toUsvfsLogLevel(log::Levels level)
{
  switch (level) {
  case log::Info:
    return LogLevel::Info;
  case log::Warning:
    return LogLevel::Warning;
  case log::Error:
    return LogLevel::Error;
  case log::Debug:  // fall-through
  default:
    return LogLevel::Debug;
  }
}

std::string usvfsLogLevelToString(LogLevel level)
{
  switch (level) {
  case LogLevel::Info:
    return "Info";
  case LogLevel::Warning:
    return "Warning";
  case LogLevel::Error:
    return "Error";
  case LogLevel::Debug:
    return "Debug";
  case LogLevel::Trace:
    return "Trace";
  default:
    return "Debug";
  }
}

UsvfsConnector::UsvfsConnector() : m_usvfsManager(UsvfsManager::instance())
{
  using namespace std::chrono;

  const auto& s = Settings::instance();

  const LogLevel logLevel = toUsvfsLogLevel(s.diagnostics().logLevel());
  const auto delay        = duration_cast<milliseconds>(s.diagnostics().spawnDelay());

  // m_usvfsManager->setUseMountNamespace(true);

  const QString logFileName =
      qApp->property("dataPath").toString() %
      QStringLiteral("/logs/usvfs-%1.log")
          .arg(QDateTime::currentDateTimeUtc().toString(u"yyyy-MM-dd_hh-mm-ss"_s));
  m_usvfsManager->setLogFile(logFileName.toStdString());
  log::debug("usvfs log messages are written to {}", logFileName);

  m_usvfsManager->setLogLevel(logLevel);
  m_usvfsManager->setProcessDelay(delay);

  log::debug("initializing usvfs:\n"
             " . instance: {}\n"
             " . log: {}",
             SHMID, usvfsLogLevelToString(logLevel));

  m_usvfsManager->clearExecutableBlacklist();
  for (const auto& exec : s.executablesBlacklist().split(';')) {
    m_usvfsManager->blacklistExecutable(exec.toStdString());
  }

  m_usvfsManager->clearExecutableBlacklist();
  for (const auto& suffix : s.skipFileSuffixes()) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_usvfsManager->addSkipFileSuffix(suffix.toStdString());
  }

  m_usvfsManager->clearSkipFileSuffixes();
  for (const auto& dir : s.skipDirectories()) {
    m_usvfsManager->addSkipDirectory(dir.toStdString());
  }

  m_usvfsManager->clearLibraryForceLoads();
}

UsvfsConnector::~UsvfsConnector()
{
  if (m_usvfsManager->isMounted()) {
    m_usvfsManager->unmount();
  }
}

void UsvfsConnector::updateMapping(const MappingType& mapping)
{
  const auto start = std::chrono::high_resolution_clock::now();

  QProgressDialog progress(qApp->activeWindow());
  progress.setLabelText(tr("Preparing vfs"));
  progress.setMaximum(static_cast<int>(mapping.size()));
  progress.show();

  int value = 0;
  int files = 0;
  int dirs  = 0;

  log::debug("Updating VFS mappings...");

  m_usvfsManager->clearVirtualMappings();

  for (const auto& map : mapping) {
    if (progress.wasCanceled()) {
      m_usvfsManager->clearVirtualMappings();
      throw UsvfsConnectorException(u"VFS mapping canceled by user"_s);
    }
    progress.setValue(value++);
    if (value % 10 == 0) {
      QCoreApplication::processEvents();
    }

    try {
      if (map.isDirectory) {
        m_usvfsManager->virtualLinkDirectoryStatic(
            map.source.toStdString(), map.destination.toStdString(),
            map.createTarget ? linkFlag::CREATE_TARGET : 0);
        ++dirs;
      } else {
        m_usvfsManager->virtualLinkFile(map.source.toStdString(),
                                        map.destination.toStdString());
        ++files;
      }
    } catch (const std::runtime_error& ex) {
      m_usvfsManager->clearVirtualMappings();
      throw UsvfsConnectorException(ex.what());
    }
  }

  const auto end  = std::chrono::high_resolution_clock::now();
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  log::debug("VFS mappings updated, linked {} dirs and {} files in {}ms", dirs, files,
             time.count());
}

void UsvfsConnector::updateParams(log::Levels logLevel,
                                  env::CoreDumpTypes /*coreDumpType*/,
                                  const QString& /*crashDumpsPath*/,
                                  std::chrono::seconds spawnDelay,
                                  QString executableBlacklist,
                                  const QStringList& skipFileSuffixes,
                                  const QStringList& skipDirectories)
{
  using namespace std::chrono;

  m_usvfsManager->setDebugMode(false);
  m_usvfsManager->setLogLevel(toUsvfsLogLevel(logLevel));
  m_usvfsManager->setProcessDelay(duration_cast<milliseconds>(spawnDelay));

  m_usvfsManager->clearExecutableBlacklist();
  for (const auto& exec : executableBlacklist.split(';')) {
    m_usvfsManager->blacklistExecutable(exec.toStdString());
  }

  m_usvfsManager->clearSkipFileSuffixes();
  for (auto& suffix : skipFileSuffixes) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_usvfsManager->addSkipFileSuffix(suffix.toStdString());
  }

  m_usvfsManager->clearSkipDirectories();
  for (auto& dir : skipDirectories) {
    m_usvfsManager->addSkipDirectory(dir.toStdString());
  }
}

void UsvfsConnector::updateForcedLibraries(
    const QList<ExecutableForcedLoadSetting>& forcedLibraries)
{
  m_usvfsManager->clearLibraryForceLoads();
  for (const auto& setting : forcedLibraries) {
    if (setting.enabled()) {
      m_usvfsManager->forceLoadLibrary(setting.process().toStdString(),
                                       setting.library().toStdString());
    }
  }
}

std::vector<HANDLE> getRunningUSVFSProcesses()
{
  const auto& pids = UsvfsManager::instance()->getVFSProcessList();

  std::vector<HANDLE> result;

  for (const auto& pid : pids) {
    siginfo_t info{};
    if (waitid(P_PID, pid, &info, WEXITED | WSTOPPED | WNOHANG | WNOWAIT) == 0) {
      if (info.si_pid == 0) {
        result.emplace_back(pidfd_open(pid, 0));
      }
    }
  }
  return result;
}
