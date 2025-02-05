#include "loot.h"
#include "json.h"
#include "lootdialog.h"
#include "organizercore.h"
#include <log.h>
#include <report.h>

using namespace MOBase;
using namespace json;

static QString LootReportPath  = QDir::temp().absoluteFilePath("lootreport.json");

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
    s += "## " + tr("Loot failed to run") + "\n";

    if (errors.empty() && warnings.empty()) {
      s += tr("No errors were reported. The log below might have more information.\n");
    }
  }

  s += errorsMarkdown();

  if (okay) {
    s += "\n" + successMarkdown();
  }

  return s;
}

QString Loot::Report::successMarkdown() const
{
  QString s;

  if (!messages.empty()) {
    s += "### " + QObject::tr("General messages") + "\n";

    for (auto&& m : messages) {
      s += " - " + m.toMarkdown() + "\n";
    }
  }

  if (!plugins.empty()) {
    if (!s.isEmpty()) {
      s += "\n";
    }

    s += "### " + QObject::tr("Plugins") + "\n";

    for (auto&& p : plugins) {
      const auto ps = p.toMarkdown();
      if (!ps.isEmpty()) {
        s += ps + "\n";
      }
    }
  }

  if (s.isEmpty()) {
    s += "**" + QObject::tr("No messages.") + "**\n";
  }

  s += stats.toMarkdown();

  return s;
}

QString Loot::Report::errorsMarkdown() const
{
  QString s;

  if (!errors.empty()) {
    s += "### " + tr("Errors") + ":\n";

    for (auto&& e : errors) {
      s += " - " + e + "\n";
    }
  }

  if (!warnings.empty()) {
    if (!s.isEmpty()) {
      s += "\n";
    }

    s += "### " + tr("Warnings") + ":\n";

    for (auto&& w : warnings) {
      s += " - " + w + "\n";
    }
  }

  return s;
}

QString Loot::Stats::toMarkdown() const
{
  return QString("`stats: %1s, lootcli %2, loot %3`")
      .arg(QString::number(time / 1000.0, 'f', 2))
      .arg(lootcliVersion)
      .arg(lootVersion);
}

QString Loot::Plugin::toMarkdown() const
{
  QString s;

  if (!incompatibilities.empty()) {
    s += " - **" + QObject::tr("Incompatibilities") + ": ";

    QString fs;
    for (auto&& f : incompatibilities) {
      if (!fs.isEmpty()) {
        fs += ", ";
      }

      fs += f.displayName.isEmpty() ? f.name : f.displayName;
    }

    s += fs + "**\n";
  }

  if (!missingMasters.empty()) {
    s += " - **" + QObject::tr("Missing masters") + ": ";

    QString ms;
    for (auto&& m : missingMasters) {
      if (!ms.isEmpty()) {
        ms += ", ";
      }

      ms += m;
    }

    s += ms + "**\n";
  }

  for (auto&& m : messages) {
    s += " - " + m.toMarkdown() + "\n";
  }

  for (auto&& d : dirty) {
    s += " - " + d.toMarkdown(false) + "\n";
  }

  if (!s.isEmpty()) {
    s = "#### " + name + "\n" + s;
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
    s += " " + info;
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
    s += "**" + QObject::tr("Error") + "**: ";
    break;
  }

  case log::Warning: {
    s += "**" + QObject::tr("Warning") + "**: ";
    break;
  }

  default: {
    break;
  }
  }

  s += text;

  return s;
}

Loot::Loot(OrganizerCore& core)
    : m_core(core), m_thread(nullptr), m_cancel(false), m_result(false)
{}

Loot::~Loot()
{
  if (m_thread) {
    m_thread->wait();
  }

  deleteReportFile();
}

void Loot::cancel()
{
  if (!m_cancel) {
    log::debug("loot received cancel request");
    m_cancel = true;
  }
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

void Loot::lootThread()
{
  try {
    m_result = false;

    if (waitForCompletion()) {
      m_result = true;
    }

    m_report = createReport();
  } catch (...) {
    log::error("unhandled exception in loot thread");
  }

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
    emit log(MOBase::log::Error, QString("failed to open file, %1 (error %2)")
                                     .arg(outFile.errorString())
                                     .arg(outFile.error()));

    return;
  }

  QJsonParseError e;
  const QJsonDocument doc = QJsonDocument::fromJson(outFile.readAll(), &e);
  if (doc.isNull()) {
    emit log(MOBase::log::Error,
             QString("invalid json, %1 (error %2)").arg(e.errorString()).arg(e.error));

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

  if (plugin.contains("incompatibilities")) {
    p.incompatibilities = reportFiles(getOpt<QJsonArray>(plugin, "incompatibilities"));
  }

  if (plugin.contains("messages")) {
    p.messages = reportMessages(getOpt<QJsonArray>(plugin, "messages"));
  }

  if (plugin.contains("dirty")) {
    p.dirty = reportDirty(getOpt<QJsonArray>(plugin, "dirty"));
  }

  if (plugin.contains("clean")) {
    p.clean = reportDirty(getOpt<QJsonArray>(plugin, "clean"));
  }

  if (plugin.contains("missingMasters")) {
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

    if (type == "info") {
      m.type = log::Info;
    } else if (type == "warn") {
      m.type = log::Warning;
    } else if (type == "error") {
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
