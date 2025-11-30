#include "overlayfsconnector.h"
#include "organizercore.h"
#include "overlayfs/overlayfsmanager.h"
#include "settings.h"
#include "shared/util.h"
#include "stub.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QProgressDialog>
#include <iomanip>
#include <log.h>
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

OverlayfsConnector::OverlayfsConnector()
    : m_overlayfsManager(OverlayFsManager::getInstance(
          qApp->property("dataPath").toString() +
          QStringLiteral("/logs/overlayfs-%1.log")
              .arg(QDateTime::currentDateTimeUtc().toString(u"yyyy-MM-dd_hh-mm-ss"_s))))
{
  const auto& s = Settings::instance();

  log::Levels level = s.diagnostics().logLevel();
  m_overlayfsManager.setLogLevel(static_cast<LogLevel>(level));

  log::debug("initializing overlayfs:\n"
             " . instance: {}\n"
             " . log: {}",
             SHMID, log::levelToString(level));

  for (const auto& suffix : s.skipFileSuffixes()) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_overlayfsManager.addSkipFileSuffix(suffix);
  }

  for (const auto& dir : s.skipDirectories()) {
    m_overlayfsManager.addSkipDirectory(dir);
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
      m_overlayfsManager.addDirectory(map.source, map.destination);
      ++dirs;
    } else {
      m_overlayfsManager.addFile(map.source, map.destination);
      ++files;
    }
  }

  const auto end  = std::chrono::high_resolution_clock::now();
  const auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  log::debug("Overlayfs mappings updated, linked {} dirs and {} files in {}ms", dirs,
             files, time.count());
}

void OverlayfsConnector::updateParams(MOBase::log::Levels logLevel,
                                      env::CoreDumpTypes /*coreDumpType*/,
                                      const QString& /*crashDumpsPath*/,
                                      std::chrono::seconds /*spawnDelay*/,
                                      QString executableBlacklist,
                                      const QStringList& skipFileSuffixes,
                                      const QStringList& skipDirectories)
{
  using namespace std::chrono;

  m_overlayfsManager.setDebugMode(false);
  m_overlayfsManager.setLogLevel(static_cast<LogLevel>(logLevel));

  m_overlayfsManager.clearSkipFileSuffixes();
  for (auto& suffix : skipFileSuffixes) {
    if (suffix.isEmpty()) {
      continue;
    }
    m_overlayfsManager.addSkipFileSuffix(suffix);
  }

  m_overlayfsManager.clearSkipDirectories();
  for (auto& dir : skipDirectories) {
    m_overlayfsManager.addSkipDirectory(dir);
  }
}

void OverlayfsConnector::updateForcedLibraries(
    const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries)
{
  m_overlayfsManager.clearLibraryForceLoads();
  for (const auto& setting : forcedLibraries) {
    if (setting.enabled()) {
      m_overlayfsManager.forceLoadLibrary(setting.process(), setting.library());
    }
  }
}

void OverlayfsConnector::setOverwritePath(const QString& path) const
{
  m_overlayfsManager.setUpperDir(path);
}

std::vector<HANDLE> getRunningOverlayfsProcesses()
{
  return OverlayFsManager::getInstance().getOverlayFsProcessList();
}
