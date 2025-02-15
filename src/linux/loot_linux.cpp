#include "json.h"
#include "loot.h"
#include "lootdialog.h"
#include "organizercore.h"
#include <log.h>
#include <report.h>

#include <utility>

using namespace MOBase;
using namespace json;
using namespace std::string_literals;
using namespace Qt::StringLiterals;

#ifdef _WIN32
static inline const QString lootExecutable = QStringLiteral("lootcli.exe");
#else
static inline const QString lootExecutable = QStringLiteral("lootcli");
#endif

static QString LootReportPath = QDir::temp().absoluteFilePath(u"lootreport.json"_s);

extern log::Levels levelFromLoot(lootcli::LogLevels level);

bool Loot::start(QWidget* parent, bool didUpdateMasterList)
{
  deleteReportFile();

  log::debug("starting loot");

  // vfs
  m_core.prepareVFS();

  // spawning
  if (!spawnLootcli(parent, didUpdateMasterList)) {
    return false;
  }

  // starting thread
  log::debug("starting loot thread");
  m_thread.reset(QThread::create([&] {
    lootThread();
  }));
  m_thread->start();

  return true;
}

bool Loot::spawnLootcli(QWidget* parent, bool didUpdateMasterList)
{
  const auto logLevel = m_core.settings().diagnostics().lootLogLevel();

  QStringList parameters;

  if (didUpdateMasterList) {
    parameters << "--skipUpdateMasterlist";
  }
  parameters << "--game" << m_core.managedGame()->lootGameName() << "--gamePath"
             << m_core.managedGame()->gameDirectory().absolutePath()
             << "--pluginListPath"
             << QString(u"%1/loadorder.txt"_s).arg(m_core.profilePath()) << "--logLevel"
             << QString::fromStdString(lootcli::logLevelToString(logLevel)) << "--out"
             << LootReportPath << "--language"
             << m_core.settings().interface().language();
  auto lootHandle = std::make_unique<QProcess>(parent);
  QString program = qApp->applicationDirPath() % u"/loot/"_s % lootExecutable;
  lootHandle->setWorkingDirectory(qApp->applicationDirPath() + "/loot");
  lootHandle->setArguments(parameters);
  lootHandle->setProgram(program);

  if (!OverlayfsManager::getInstance().mount()) {
    emit log(log::Levels::Error, tr("failed to start loot: error mounting overlayfs"));
    return false;
  }

  lootHandle->start();

  // wait for up to 2sec
  if (!lootHandle->waitForStarted(2000)) {
    emit log(log::Levels::Error,
             tr("failed to start loot: %1").arg(lootHandle->errorString()));
    log::error("failed to start loot: {}", lootHandle->errorString().toStdString());
    return false;
  }

  emit log(log::Levels::Debug, u"loot started"_s);

  m_lootProcess = std::move(lootHandle);
  connect(m_lootProcess.get(), SIGNAL(readyReadStandardOutput()), this,
          SLOT(processStdout()));

  return true;
}

bool Loot::waitForCompletion()
{
  log::debug("loot thread waiting for completion on lootcli");

  for (;;) {
    bool res = m_lootProcess->waitForFinished(100);

    if (res) {
      log::debug("lootcli has completed");
      // done
      break;
    }

    if (m_lootProcess->state() == QProcess::NotRunning) {
      log::error("failed to wait on loot process, {}", m_lootProcess->errorString());
      return false;
    }

    if (m_cancel) {
      log::debug("terminating lootcli process");
      m_lootProcess->terminate();

      log::debug("waiting for loocli process to terminate");
      m_lootProcess->waitForFinished(-1);

      m_lootProcess.reset();
      log::debug("lootcli terminated");
      return false;
    }
  }

  if (m_cancel) {
    return false;
  }

  processStdout();

  // checking exit code
  auto exitStatus = m_lootProcess->exitStatus();
  int exitCode    = m_lootProcess->exitCode();

  if (exitStatus == QProcess::CrashExit) {
    log::error("Loot crashed, {}", m_lootProcess->errorString());
    return false;
  }

  if (exitCode != 0UL) {
    emit log(log::Levels::Error,
             tr("Loot failed. Exit code was: 0x%1").arg(exitCode, 0, 16));
    return false;
  }

  return true;
}

void Loot::processStdout()
{
  QString lootOut = m_lootProcess->readAllStandardOutput();
  emit output(lootOut);
  QStringList lines = lootOut.split('\n');
  for (const auto& line : lines) {
    const auto m = lootcli::parseMessage(line.toStdString());

    if (m.type == lootcli::MessageType::None) {
      log::error("unrecognised loot output: '{}'", line);
      continue;
    }

    processMessage(m);
  }
}
