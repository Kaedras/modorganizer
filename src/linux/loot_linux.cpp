#include "json.h"
#include "loot.h"
#include "lootdialog.h"
#include "organizercore.h"
#include <log.h>
#include <report.h>

using namespace MOBase;
using namespace json;
using namespace Qt::StringLiterals;

#ifdef __unix__
static inline const QString lootExecutable = QStringLiteral("lootcli");
#else
static inline const QString lootExecutable = QStringLiteral("lootcli.exe");
#endif

static const QString LootReportPath =
    QDir::temp().absoluteFilePath(u"lootreport.json"_s);

log::Levels levelFromLoot(lootcli::LogLevels level)
{
  using LC = lootcli::LogLevels;

  switch (level) {
  case LC::Trace:  // fall-through
  case LC::Debug:
    return log::Debug;

  case LC::Info:
    return log::Info;

  case LC::Warning:
    return log::Warning;

  case LC::Error:
    return log::Error;

  default:
    return log::Info;
  }
}

QString Loot::Report::toMarkdown() const
{
  QString s;

  if (!okay) {
    s += u"## "_s % tr("Loot failed to run") % u"\n"_s;

    if (errors.empty() && warnings.empty()) {
      s += tr("No errors were reported. The log below might have more information.\n");
    }
  }

  s += errorsMarkdown();

  if (okay) {
    s += u"\n"_s % successMarkdown();
  }

  return s;
}

QString Loot::Report::successMarkdown() const
{
  QString s;

  if (!messages.empty()) {
    s += u"### "_s % QObject::tr("General messages") % u"\n"_s;

    for (auto&& m : messages) {
      s += u" - "_s % m.toMarkdown() % u"\n"_s;
    }
  }

  if (!plugins.empty()) {
    if (!s.isEmpty()) {
      s += u"\n"_s;
    }

    s += u"### "_s % QObject::tr("Plugins") % u"\n"_s;

    for (auto&& p : plugins) {
      const auto ps = p.toMarkdown();
      if (!ps.isEmpty()) {
        s += ps % u"\n"_s;
      }
    }
  }

  if (s.isEmpty()) {
    s += u"**"_s % QObject::tr("No messages.") % u"**\n"_s;
  }

  s += stats.toMarkdown();

  return s;
}

QString Loot::Report::errorsMarkdown() const
{
  QString s;

  if (!errors.empty()) {
    s += u"### "_s % tr("Errors") % u":\n"_s;

    for (auto&& e : errors) {
      s += u" - "_s % e % u"\n"_s;
    }
  }

  if (!warnings.empty()) {
    if (!s.isEmpty()) {
      s += u"\n"_s;
    }

    s += u"### "_s % tr("Warnings") % u":\n"_s;

    for (auto&& w : warnings) {
      s += u" - "_s % w + u"\n"_s;
    }
  }

  return s;
}

QString Loot::Stats::toMarkdown() const
{
  return QStringLiteral("`stats: %1s, lootcli %2, loot %3`")
      .arg(QString::number(time / 1000.0, 'f', 2), lootcliVersion, lootVersion);
}

QString Loot::Plugin::toMarkdown() const
{
  QString s;

  if (!incompatibilities.empty()) {
    s += u" - **"_s % QObject::tr("Incompatibilities") % u": "_s;

    QString fs;
    for (auto&& f : incompatibilities) {
      if (!fs.isEmpty()) {
        fs += u", "_s;
      }

      fs += f.displayName.isEmpty() ? f.name : f.displayName;
    }

    s += fs % u"**\n"_s;
  }

  if (!missingMasters.empty()) {
    s += u" - **"_s % QObject::tr("Missing masters") % u": "_s;

    QString ms;
    for (auto&& m : missingMasters) {
      if (!ms.isEmpty()) {
        ms += u", "_s;
      }

      ms += m;
    }

    s += ms % u"**\n"_s;
  }

  for (auto&& m : messages) {
    s += u" - "_s % m.toMarkdown() % u"\n"_s;
  }

  for (auto&& d : dirty) {
    s += u" - "_s % d.toMarkdown(false) % u"\n"_s;
  }

  if (!s.isEmpty()) {
    s = u"#### "_s % name % u"\n"_s % s;
  }

  return s;
}

QString Loot::Dirty::toString(bool isClean) const
{
  if (isClean) {
    return QObject::tr("Verified clean by %1")
        .arg(cleaningUtility.isEmpty() ? "?" : cleaningUtility);
  }

  QString s = cleaningString();

  if (!info.isEmpty()) {
    s += u" "_s % info;
  }

  return s;
}

QString Loot::Dirty::toMarkdown(bool isClean) const
{
  return toString(isClean);
}

QString Loot::Dirty::cleaningString() const
{
  return QObject::tr("%1 found %2 ITM record(s), %3 deleted reference(s) and %4 "
                     "deleted navmesh(es).")
      .arg(cleaningUtility.isEmpty() ? "?" : cleaningUtility)
      .arg(itm)
      .arg(deletedReferences)
      .arg(deletedNavmesh);
}

QString Loot::Message::toMarkdown() const
{
  QString s;

  switch (type) {
  case log::Error: {
    s += u"**"_s % QObject::tr("Error") % u"**: "_s;
    break;
  }

  case log::Warning: {
    s += u"**"_s % QObject::tr("Warning") % u"**: "_s;
    break;
  }

  default: {
    break;
  }
  }

  s += text;

  return s;
}

Loot::Loot(OrganizerCore& core) : m_core(core), m_cancel(false), m_result(false) {}

Loot::~Loot()
{
  if (m_lootProcess) {
    if (m_lootProcess->state() == QProcess::Running) {
      m_lootProcess->waitForFinished(-1);
    }
  }

  deleteReportFile();
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

  if (!OverlayfsManager::getInstance().mount()) {
    emit log(log::Levels::Error, tr("failed to start loot: error mounting overlayfs"));
    return false;
  }

  QStringList parameters;

  if (didUpdateMasterList) {
    parameters << u"--skipUpdateMasterlist"_s;
  }
  parameters << u"--game"_s << m_core.managedGame()->lootGameName() << u"--gamePath"_s
             << m_core.managedGame()->gameDirectory().absolutePath()
             << u"--pluginListPath"_s
             << QString(u"%1/loadorder.txt"_s).arg(m_core.profilePath())
             << u"--logLevel"_s << QString::fromStdString(logLevelToString(logLevel))
             << u"--out"_s << LootReportPath << u"--language"_s
             << m_core.settings().interface().language();
  auto lootHandle = std::make_unique<QProcess>(parent);
  QString program = qApp->applicationDirPath() % u"/loot/"_s % lootExecutable;
  lootHandle->setWorkingDirectory(qApp->applicationDirPath() % u"/loot"_s);
  lootHandle->setArguments(parameters);
  lootHandle->setProgram(program);
  lootHandle->start();

  // wait for up to 500ms
  if (!lootHandle->waitForStarted(500)) {
    emit log(log::Levels::Error,
             tr("failed to start loot: %1").arg(lootHandle->errorString()));
    log::error("failed to start loot: {}", lootHandle->errorString().toStdString());
    return false;
  }

  emit log(log::Levels::Debug, u"loot started"_s);

  m_lootProcess = std::move(lootHandle);
  connect(m_lootProcess.get(), &QProcess::finished, this, &Loot::onFinished,
          Qt::QueuedConnection);

  return true;
}

void Loot::cancel()
{
  if (m_cancel) {
    return;
  }

  m_cancel = true;
  log::debug("loot received cancel request");
  log::debug("terminating lootcli process");
  m_lootProcess->terminate();
}

bool Loot::result() const
{
  return m_result;
}

const QString& Loot::outPath() const
{
  return LootReportPath;
}

const Loot::Report& Loot::report() const
{
  return m_report;
}

const std::vector<QString>& Loot::errors() const
{
  return m_errors;
}

const std::vector<QString>& Loot::warnings() const
{
  return m_warnings;
}

void Loot::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
  OverlayfsManager::getInstance().umount();

  if (exitStatus == QProcess::CrashExit) {
    if (m_cancel) {
      m_lootProcess.reset();
      log::debug("lootcli terminated");
      return;
    }
    log::error("Loot crashed, {}", m_lootProcess->errorString());
    return;
  }

  if (exitCode != 0UL) {
    emit log(log::Levels::Error,
             tr("Loot failed. Exit code was: 0x%1").arg(exitCode, 0, 16));
    return;
  }

  log::debug("lootcli has completed");

  QString lootOut = m_lootProcess->readAllStandardOutput();
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

  m_result = true;
  m_report = createReport();
  log::debug("finishing loot thread");
  emit finished();
}

void Loot::processMessage(const lootcli::Message& m)
{
  switch (m.type) {
  case lootcli::MessageType::Log: {
    const auto level = levelFromLoot(m.logLevel);

    if (level == log::Error) {
      m_errors.push_back(QString::fromStdString(m.log));
    } else if (level == log::Warning) {
      m_warnings.push_back(QString::fromStdString(m.log));
    }

    emit log(level, QString::fromStdString(m.log));
    break;
  }

  case lootcli::MessageType::Progress: {
    emit progress(m.progress);
    break;
  }
  }
}

Loot::Report Loot::createReport() const
{
  Report r;

  r.okay     = m_result;
  r.errors   = m_errors;
  r.warnings = m_warnings;

  if (m_result) {
    processOutputFile(r);
  }

  return r;
}

void Loot::deleteReportFile()
{
  if (QFile::exists(LootReportPath)) {
    log::debug("deleting temporary loot report '{}'", LootReportPath);
    const auto r = shell::Delete(QFileInfo(LootReportPath));

    if (!r) {
      log::error("failed to remove temporary loot json report '{}': {}", LootReportPath,
                 r.toString());
    }
  }
}

void Loot::processOutputFile(Report& r) const
{
  log::debug("parsing json output file at '{}'", LootReportPath);

  QFile outFile(LootReportPath);
  if (!outFile.open(QIODevice::ReadOnly)) {
    emit log(MOBase::log::Error, QStringLiteral("failed to open file, %1 (error %2)")
                                     .arg(outFile.errorString(), outFile.error()));

    return;
  }

  QJsonParseError e;
  const QJsonDocument doc = QJsonDocument::fromJson(outFile.readAll(), &e);
  if (doc.isNull()) {
    emit log(
        MOBase::log::Error,
        QStringLiteral("invalid json, %1 (error %2)").arg(e.errorString(), e.error));

    return;
  }

  requireObject(doc, "root");

  const QJsonObject object = doc.object();

  r.messages = reportMessages(getOpt<QJsonArray>(object, "messages"));
  r.plugins  = reportPlugins(getOpt<QJsonArray>(object, "plugins"));
  r.stats    = reportStats(getWarn<QJsonObject>(object, "stats"));
}

std::vector<Loot::Plugin> Loot::reportPlugins(const QJsonArray& plugins) const
{
  std::vector<Loot::Plugin> v;

  for (auto pluginValue : plugins) {
    const auto o = convertWarn<QJsonObject>(pluginValue, "plugin");
    if (o.isEmpty()) {
      continue;
    }

    auto p = reportPlugin(o);
    if (!p.name.isEmpty()) {
      v.emplace_back(std::move(p));
    }
  }

  return v;
}

Loot::Plugin Loot::reportPlugin(const QJsonObject& plugin) const
{
  Plugin p;

  p.name = getWarn<QString>(plugin, "name");
  if (p.name.isEmpty()) {
    return {};
  }

  // ignore disabled plugins; lootcli doesn't know if a plugin is enabled or not
  // and will report information on any plugin that's in the filesystem
  if (!m_core.pluginList()->isEnabled(p.name)) {
    return {};
  }

  if (plugin.contains(u"incompatibilities"_s)) {
    p.incompatibilities = reportFiles(getOpt<QJsonArray>(plugin, "incompatibilities"));
  }

  if (plugin.contains(u"messages"_s)) {
    p.messages = reportMessages(getOpt<QJsonArray>(plugin, "messages"));
  }

  if (plugin.contains(u"dirty"_s)) {
    p.dirty = reportDirty(getOpt<QJsonArray>(plugin, "dirty"));
  }

  if (plugin.contains(u"clean"_s)) {
    p.clean = reportDirty(getOpt<QJsonArray>(plugin, "clean"));
  }

  if (plugin.contains(u"missingMasters"_s)) {
    p.missingMasters = reportStringArray(getOpt<QJsonArray>(plugin, "missingMasters"));
  }

  p.loadsArchive  = getOpt(plugin, "loadsArchive", false);
  p.isMaster      = getOpt(plugin, "isMaster", false);
  p.isLightMaster = getOpt(plugin, "isLightMaster", false);

  return p;
}

Loot::Stats Loot::reportStats(const QJsonObject& stats) const
{
  Stats s;

  s.time           = getWarn<qint64>(stats, "time");
  s.lootcliVersion = getWarn<QString>(stats, "lootcliVersion");
  s.lootVersion    = getWarn<QString>(stats, "lootVersion");

  return s;
}

std::vector<Loot::Message> Loot::reportMessages(const QJsonArray& array) const
{
  std::vector<Loot::Message> v;

  for (auto messageValue : array) {
    const auto o = convertWarn<QJsonObject>(messageValue, "message");
    if (o.isEmpty()) {
      continue;
    }

    Message m;

    const auto type = getWarn<QString>(o, "type");

    if (type == "info"_L1) {
      m.type = log::Info;
    } else if (type == "warn"_L1) {
      m.type = log::Warning;
    } else if (type == "error"_L1) {
      m.type = log::Error;
    } else {
      log::error("unknown message type '{}'", type);
      m.type = log::Info;
    }

    m.text = getWarn<QString>(o, "text");

    if (!m.text.isEmpty()) {
      v.emplace_back(std::move(m));
    }
  }

  return v;
}

std::vector<Loot::File> Loot::reportFiles(const QJsonArray& array) const
{
  std::vector<Loot::File> v;

  for (auto&& fileValue : array) {
    const auto o = convertWarn<QJsonObject>(fileValue, "file");
    if (o.isEmpty()) {
      continue;
    }

    File f;

    f.name        = getWarn<QString>(o, "name");
    f.displayName = getOpt<QString>(o, "displayName");

    if (!f.name.isEmpty()) {
      v.emplace_back(std::move(f));
    }
  }

  return v;
}

std::vector<Loot::Dirty> Loot::reportDirty(const QJsonArray& array) const
{
  std::vector<Loot::Dirty> v;

  for (auto&& dirtyValue : array) {
    const auto o = convertWarn<QJsonObject>(dirtyValue, "dirty");

    Dirty d;

    d.crc               = getWarn<qint64>(o, "crc");
    d.itm               = getOpt<qint64>(o, "itm");
    d.deletedReferences = getOpt<qint64>(o, "deletedReferences");
    d.deletedNavmesh    = getOpt<qint64>(o, "deletedNavmesh");
    d.cleaningUtility   = getOpt<QString>(o, "cleaningUtility");
    d.info              = getOpt<QString>(o, "info");

    v.emplace_back(std::move(d));
  }

  return v;
}

std::vector<QString> Loot::reportStringArray(const QJsonArray& array) const
{
  std::vector<QString> v;

  for (auto&& sv : array) {
    auto s = convertWarn<QString>(sv, "string");
    if (s.isEmpty()) {
      continue;
    }

    v.emplace_back(std::move(s));
  }

  return v;
}

bool runLoot(QWidget* parent, OrganizerCore& core, bool didUpdateMasterList)
{
  core.savePluginList();

  try {
    Loot loot(core);
    LootDialog dialog(parent, core, loot);

    if (!loot.start(parent, didUpdateMasterList)) {
      return false;
    }

    dialog.exec();

    return dialog.result();
  } catch (const UsvfsConnectorException& e) {
    log::debug("{}", e.what());
    return false;
  } catch (const std::exception& e) {
    reportError(QObject::tr("failed to run loot: %1").arg(e.what()));
    return false;
  }
}
