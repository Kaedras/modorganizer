#include "json.h"
#include "loot.h"
#include "organizercore.h"
#include <QApplication>
#include <usvfs-fuse/usvfsmanager.h>

using namespace MOBase;
using namespace json;
using namespace Qt::StringLiterals;

static const QString LootReportPath =
    QDir::temp().absoluteFilePath(u"lootreport.json"_s);

Loot::Loot(OrganizerCore& core) : m_core(core), m_cancel(false), m_result(false) {}

Loot::~Loot()
{
  if (m_lootProcess) {
    if (m_lootProcess->state() == QProcess::Running) {
      m_lootProcess->waitForFinished(-1);
    }
  }

  deleteReportFile();
  deleteSortedLoadOrder();
}

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

  return true;
}

bool Loot::spawnLootcli(QWidget* parent, bool didUpdateMasterList)
{
  const auto logLevel = m_core.settings().diagnostics().lootLogLevel();

  if (!UsvfsManager::instance()->mount()) {
    emit log(log::Levels::Error, tr("failed to start loot: error mounting usvfs"));
    return false;
  }

  QStringList parameters;

  if (didUpdateMasterList) {
    parameters << u"--skipUpdateMasterlist"_s;
  }
  parameters << u"--game"_s << m_core.managedGame()->lootGameName() << u"--gamePath"_s
             << m_core.managedGame()->gameDirectory().absolutePath()
             << u"--pluginListPath"_s
             << QStringLiteral("%1/loadorder.txt").arg(m_core.profilePath())
             << u"--logLevel"_s << QString::fromStdString(logLevelToString(logLevel))
             << u"--out"_s << LootReportPath << u"--language"_s
             << m_core.settings().interface().language();
  auto lootHandle = std::make_unique<QProcess>(parent);
  QString program = qApp->applicationDirPath() % "/loot/lootcli"_L1;
  lootHandle->setWorkingDirectory(qApp->applicationDirPath() % "/loot"_L1);
  lootHandle->setArguments(parameters);
  lootHandle->setProgram(program);
  lootHandle->start();

  // wait for up to 1 second
  if (!lootHandle->waitForStarted(1000)) {
    emit log(log::Levels::Error,
             tr("failed to start loot: %1").arg(lootHandle->errorString()));
    return false;
  }

  emit log(log::Levels::Debug, u"loot started"_s);

  m_lootProcess = std::move(lootHandle);
  connect(m_lootProcess.get(), &QProcess::finished, this, &Loot::onFinished,
          Qt::QueuedConnection);
  connect(m_lootProcess.get(), &QProcess::readyReadStandardOutput, this,
          &Loot::onReadyReadStandardOutput, Qt::QueuedConnection);
  connect(m_lootProcess.get(), &QProcess::readyReadStandardError, this,
          &Loot::onReadyReadStandardError, Qt::QueuedConnection);

  return true;
}

void Loot::processStdout(const QString& lootOut)
{
  emit output(lootOut);

  QStringList lines = lootOut.split('\n');
  for (const auto& line : lines) {
    // skip empty lines
    if (line.isEmpty()) {
      continue;
    }

    const auto m = lootcli::parseMessage(line.toStdString());

    if (m.type == lootcli::MessageType::None) {
      log::error("unrecognised loot output: '{}'", line);
      continue;
    }

    processMessage(m);
  }
}

void Loot::processStderr(const QString& lootOut) const
{
  emit log(log::Error, lootOut);
}

void Loot::cancel()
{
  if (!m_cancel) {
    log::debug("loot received cancel request");
    m_cancel = true;
    m_lootProcess->terminate();
  }
}

void Loot::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  QScopeGuard g([this]() {
    log::debug("finishing loot thread");
    emit finished();
  });

  UsvfsManager::instance()->unmount();

  if (exitStatus == QProcess::CrashExit) {
    if (m_cancel) {
      m_lootProcess.reset();
      log::debug("lootcli terminated");
      return;
    }
    log::error("Loot crashed, {}", m_lootProcess->errorString());
    return;
  }

  if (exitCode != 0) {
    emit log(log::Levels::Error,
             tr("Loot failed. Exit code was: 0x%1").arg(exitCode, 0, 16));
    return;
  }

  log::debug("lootcli has completed");

  m_result = true;
  m_report = createReport();
}

void Loot::onReadyReadStandardOutput()
{
  QString out = m_lootProcess->readAllStandardOutput();
  processStdout(out);
}

void Loot::onReadyReadStandardError() const
{
  QString out = m_lootProcess->readAllStandardError();
  processStderr(out);
}
