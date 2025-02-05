#include "env.h"
#include "envdump.h"
#include "envmetrics.h"
#include "envmodule.h"
#include "envos.h"
#include "envsecurity.h"
#include "envshortcut.h"
#include "settings.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

#ifdef __unix__
static inline const QString defaultName = QStringLiteral("ModOrganizer");
inline QString GETENV(const char* varName)
{
  return qgetenv(varName);
}
#else
static inline const QString defaultName = QStringLiteral("ModOrganizer.exe");
inline QString GETENV(const char* varName)
{
  return qEnvironmentVariable(varName);
}
#endif

namespace env
{

using namespace MOBase;


ModuleNotification::ModuleNotification(QObject* o, std::function<void(Module)> f)
    : m_cookie(nullptr), m_object(o), m_f(std::move(f))
{}


void ModuleNotification::setCookie(void* c)
{
  m_cookie = c;
}

void ModuleNotification::fire(QString path, std::size_t fileSize)
{
  if (m_loaded.contains(path)) {
    // don't notify if it's been loaded before
    return;
  }

  m_loaded.insert(path);

  // constructing a Module will query the version info of the file, which seems
  // to generate an access violation for at least plugin_python.dll on Windows 7
  //
  // it's not clear what the problem is, but making sure this is deferred until
  // _after_ the dll is loaded seems to fix it
  //
  // so this queues the callback in the main thread

  if (m_f) {
    QMetaObject::invokeMethod(
        m_object,
        [path, fileSize, f = m_f] {
          f(Module(path, fileSize));
        },
        Qt::QueuedConnection);
  }
}

Environment::Environment() {}

// anchor
Environment::~Environment() = default;

const std::vector<Module>& Environment::loadedModules() const
{
  if (m_modules.empty()) {
    m_modules = getLoadedModules();
  }

  return m_modules;
}

std::vector<Process> Environment::runningProcesses() const
{
  return getRunningProcesses();
}

const OsInfo& Environment::getOsInfo() const
{
  if (!m_os) {
    m_os.reset(CreateInfo());
  }

  return *m_os;
}

const std::vector<SecurityProduct>& Environment::securityProducts() const
{
  if (m_security.empty()) {
    m_security = getSecurityProducts();
  }

  return m_security;
}

const Metrics& Environment::metrics() const
{
  if (!m_metrics) {
    m_metrics.reset(new Metrics);
  }

  return *m_metrics;
}

QString Environment::timezone() const
{
  QTimeZone timeZone(QTimeZone::LocalTime);

  auto offsetString = [](int o) {
    return QString("%1%2:%3")
        .arg(o < 0 ? "" : "+")
        .arg(QString::number(o / 60), 2, QChar::fromLatin1('0'))
        .arg(QString::number(o % 60), 2, QChar::fromLatin1('0'));
  };

  QString s;
  const auto now = QDateTime::currentDateTime();

  const auto stdName = timeZone.displayName(QTimeZone::TimeType::StandardTime);
  const auto std = QString("%1, %2").arg(stdName).arg(offsetString(timeZone.offsetFromUtc(now)));

  const auto dstName = timeZone.displayName(QTimeZone::TimeType::DaylightTime);
  const auto dst       = QString("%1, %2").arg(dstName).arg(timeZone.offsetFromUtc(now));


  if(timeZone.isDaylightTime(now)) {
    s = dst + " (dst is active, std is " + std + ")";
  } else {
    s = std + " (std is active, dst is " + dst + ")";
  }

  return s;
}


void Environment::dump(const Settings& s) const
{
  log::debug("windows: {}", getOsInfo().toString());

  log::debug("time zone: {}", timezone());

  if (getOsInfo().compatibilityMode()) {
    log::warn("MO seems to be running in compatibility mode");
  }

  log::debug("security products:");

  {
    // ignore products with identical names, some AVs register themselves with
    // the same names and provider, but different guids
    std::set<QString> productNames;
    for (const auto& sp : securityProducts()) {
      productNames.insert(sp.toString());
    }

    for (auto&& name : productNames) {
      log::debug("  . {}", name);
    }
  }

  log::debug("modules loaded in process:");
  for (const auto& m : loadedModules()) {
    if (m.interesting()) {
      log::debug(" . {}", m.toString());
    }
  }

  log::debug("displays:");
  for (const auto& d : metrics().displays()) {
    log::debug(" . {}", d->name());
  }

  const auto r = metrics().desktopGeometry();
  log::debug("desktop geometry: ({},{})-({},{})", r.left(), r.top(), r.right(),
             r.bottom());

  dumpDisks(s);
}

void Environment::dumpDisks(const Settings& s) const
{
  std::set<QString> rootPaths;

  auto dump = [&](auto&& path) {
    const QFileInfo fi(path);
    const QStorageInfo si(fi.absoluteFilePath());

    if (rootPaths.contains(si.rootPath())) {
      // already seen
      return;
    }

    // remember
    rootPaths.insert(si.rootPath());

    log::debug("  . {} free={} MB{}", si.rootPath(), (si.bytesFree() / 1000 / 1000),
               (si.isReadOnly() ? " (readonly)" : ""));
  };

  log::debug("drives:");

  dump(QStorageInfo::root().rootPath());
  dump(s.paths().base());
  dump(s.paths().downloads());
  dump(s.paths().mods());
  dump(s.paths().cache());
  dump(s.paths().profiles());
  dump(s.paths().overwrite());
  dump(QCoreApplication::applicationDirPath());
}

QString path()
{
  return get("PATH");
}

QString appendToPath(const QString& s)
{
  auto old = path();
  set("PATH", old + ";" + s);
  return old;
}

QString prependToPath(const QString& s)
{
  auto old = path();
  set("PATH", s + ";" + old);
  return old;
}

void setPath(const QString& s)
{
  set("PATH", s);
}

QString get(const QString& name)
{
  return GETENV(name.toStdString().c_str());
}

void set(const QString& n, const QString& v)
{
  // from qt documentation:
  // Calling qputenv with an empty value removes the environment variable on Windows, and makes it set (but empty) on Unix. Prefer using qunsetenv() for fully portable behavior.
  if (v.isEmpty()) {
    qunsetenv(n.toStdString().c_str());
  } else {
    qputenv(n.toStdString().c_str(), v.toLocal8Bit());
  }
}

Service::Service(QString name) : Service(std::move(name), StartType::None, Status::None)
{}

Service::Service(QString name, StartType st, Status s)
    : m_name(std::move(name)), m_startType(st), m_status(s)
{}

const QString& Service::name() const
{
  return m_name;
}

bool Service::isValid() const
{
  return (m_startType != StartType::None) && (m_status != Status::None);
}

Service::StartType Service::startType() const
{
  return m_startType;
}

Service::Status Service::status() const
{
  return m_status;
}

QString Service::toString() const
{
  return QString("service '%1', start=%2, status=%3")
      .arg(m_name)
      .arg(env::toString(m_startType))
      .arg(env::toString(m_status));
}

QString toString(Service::StartType st)
{
  using ST = Service::StartType;

  switch (st) {
  case ST::None:
    return "none";

  case ST::Disabled:
    return "disabled";

  case ST::Enabled:
    return "enabled";

  default:
    return QString("unknown %1").arg(static_cast<int>(st));
  }
}

QString toString(Service::Status st)
{
  using S = Service::Status;

  switch (st) {
  case S::None:
    return "none";

  case S::Stopped:
    return "stopped";

  case S::Running:
    return "running";

  default:
    return QString("unknown %1").arg(static_cast<int>(st));
  }
}

std::filesystem::path processPath(HANDLE process = INVALID_HANDLE_VALUE);

QString processFilename(HANDLE process = INVALID_HANDLE_VALUE);

std::filesystem::path thisProcessPath()
{
  return processPath();
}

DWORD findOtherPid()
{
  std::clog << "looking for the other process...\n";

  // used to skip the current process below
  const auto thisPid = GetCurrentProcessId();
  std::clog << "this process id is " << thisPid << "\n";

  // getting the filename for this process, assumes the other process has the
  // same one
  auto filename = processFilename();
  if (filename.isEmpty()) {
    std::cerr << "can't get current process filename, defaulting to " << defaultName.toStdString()
               << "\n";

    filename = defaultName;
  } else {
    std::clog << "this process filename is " << filename.toStdString() << "\n";
  }

  // getting all running processes
  const auto processes = getRunningProcesses();
  std::clog << "there are " << processes.size() << " processes running\n";

  // going through processes, trying to find one with the same name and a
  // different pid than this process has
  for (const auto& p : processes) {
    if (p.name() == filename) {
      if (p.pid() != thisPid) {
        return p.pid();
      }
    }
  }

  std::clog << "no process with this filename\n"
            << "MO may not be running, or it may be running as administrator\n"
            << "you can try running this again as administrator\n";

  return 0;
}

QString tempDir()
{
  return QStandardPaths::standardLocations(QStandardPaths::TempLocation).first();
}

std::wstring safeVersion()
{
  try {
    // this can throw
    return MOShared::createVersionInfo().string().toStdWString() + L"-";
  } catch (...) {
    return {};
  }
}

HandlePtr tempFile(const QString& dir);

HandlePtr dumpFile(const QString& dir);

CoreDumpTypes coreDumpTypeFromString(const std::string& s)
{
  if (s == "data")
    return env::CoreDumpTypes::Data;
  else if (s == "full")
    return env::CoreDumpTypes::Full;
  else
    return env::CoreDumpTypes::Mini;
}

std::string toString(CoreDumpTypes type)
{
  switch (type) {
  case CoreDumpTypes::Mini:
    return "mini";

  case CoreDumpTypes::Data:
    return "data";

  case CoreDumpTypes::Full:
    return "full";

  default:
    return "?";
  }
}

bool createMiniDump(const QString& dir, HANDLE process, CoreDumpTypes type);

bool coredump(const QString& dir, CoreDumpTypes type)
{
  std::wclog << L"creating minidump for the current process\n";
  return createMiniDump(dir, GetCurrentProcess(), type);
}

bool coredumpOther(CoreDumpTypes type);

}  // namespace env
