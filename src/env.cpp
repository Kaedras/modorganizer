#include "env.h"
#include "envdump.h"
#include "envmetrics.h"
#include "envmodule.h"
#include "envos.h"
#include "envsecurity.h"
#include "settings.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

#ifdef __unix__
static inline const QString defaultName = QStringLiteral("ModOrganizer");
static inline const char pathSeparator  = ':';
inline QString GETENV(const char* varName)
{
  return qgetenv(varName);
}
// write, create file, fail if file is not created
static inline constexpr int fileFlags = O_WRONLY | O_CREAT | O_EXCL;
// rw for user, group
static inline constexpr int fileMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
#else
#pragma warning(disable : 4996)
static inline const QString defaultName = QStringLiteral("ModOrganizer.exe");
static inline const char pathSeparator  = ';';
inline QString GETENV(const char* varName)
{
  return qEnvironmentVariable(varName);
}
// write, create file, fail if file is not created
static inline constexpr int fileFlags = _O_WRONLY | _O_CREAT | _O_EXCL;
// rw
static inline constexpr int fileMode = _S_IREAD | _S_IWRITE;
#endif

using namespace Qt::StringLiterals;

namespace env
{

using namespace MOBase;

extern QString processPath(HANDLE process = INVALID_HANDLE_VALUE);
extern bool createMiniDump(const QString& dir, HANDLE process, CoreDumpTypes type);

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
    m_os = CreateInfo();
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
    return QStringLiteral("%1%2:%3")
        .arg(o < 0 ? "" : "+")
        .arg(QString::number(o / 3600), 2, QChar::fromLatin1('0'))
        .arg(QString::number(o % 3600), 2, QChar::fromLatin1('0'));
  };

  QString s;
  const QDateTime now     = QDateTime::currentDateTime();
  const int currentOffset = timeZone.offsetFromUtc(now);
  const int otherOffset   = timeZone.offsetFromUtc(timeZone.nextTransition(now).atUtc);

  QString currentName, otherName;
  if (timeZone.isDaylightTime(now)) {
    currentName = timeZone.displayName(QTimeZone::TimeType::DaylightTime);
    otherName   = timeZone.displayName(QTimeZone::TimeType::StandardTime);
  } else {
    currentName = timeZone.displayName(QTimeZone::TimeType::StandardTime);
    otherName   = timeZone.displayName(QTimeZone::TimeType::DaylightTime);
  }

  const QString current =
      QStringLiteral("%1, %2").arg(currentName, offsetString(currentOffset));
  const QString other =
      QStringLiteral("%1, %2").arg(otherName, offsetString(otherOffset));

  if (timeZone.isDaylightTime(now)) {
    s = current % u" (dst is active, std is "_s % other % ')';
  } else {
    s = current % u" (std is active, dst is "_s % other % ')';
  }

  return s;
}

void Environment::dump(const Settings& s) const
{
  log::debug("os: {}", getOsInfo().toString());

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
    log::debug(" . {}*{} {} dpi={} on {}, model: {}", d->geometry().width(),
               d->geometry().height(), round(d->refreshRate()), d->logicalDotsPerInch(),
               d->name(), d->model());
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
  return get(u"PATH"_s);
}

QString appendToPath(const QString& s)
{
  auto old = path();
  set(u"PATH"_s, old % pathSeparator % s);
  return old;
}

QString prependToPath(const QString& s)
{
  auto old = path();
  set(u"PATH"_s, s % pathSeparator % old);
  return old;
}

void setPath(const QString& s)
{
  set(u"PATH"_s, s);
}

QString get(const QString& name)
{
  return GETENV(name.toStdString().c_str());
}

void set(const QString& n, const QString& v)
{
  // from qt documentation:
  // Calling qputenv with an empty value removes the environment variable on Windows,
  // and makes it set (but empty) on Unix. Prefer using qunsetenv() for fully portable
  // behavior.
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
  return QStringLiteral("service '%1', start=%2, status=%3")
      .arg(m_name, env::toString(m_startType), env::toString(m_status));
}

QString toString(Service::StartType st)
{
  using ST = Service::StartType;

  switch (st) {
  case ST::None:
    return u"none"_s;

  case ST::Disabled:
    return u"disabled"_s;

  case ST::Enabled:
    return u"enabled"_s;

  default:
    return QStringLiteral("unknown %1").arg(static_cast<int>(st));
  }
}

QString toString(Service::Status st)
{
  using S = Service::Status;

  switch (st) {
  case S::None:
    return u"none"_s;

  case S::Stopped:
    return u"stopped"_s;

  case S::Running:
    return u"running"_s;

  default:
    return QStringLiteral("unknown %1").arg(static_cast<int>(st));
  }
}

std::pair<QString, QString> splitExeAndArguments(const QString& cmd)
{
  qsizetype exeBegin = 0;
  qsizetype exeEnd   = -1;

  if (cmd[0] == '"') {
    // surrounded by double-quotes, so find the next one
    exeBegin = 1;
    exeEnd   = cmd.indexOf('"', exeBegin);

    if (exeEnd == -1) {
      log::error("missing terminating double-quote in command line '{}'", cmd);
      return {};
    }
  } else {
    // no double-quotes, find the first whitespace
    static const QRegularExpression regex(u"\\s"_s);
    exeEnd = cmd.indexOf(regex);
    if (exeEnd == -1) {
      exeEnd = cmd.size();
    }
  }

  QString exe  = cmd.mid(exeBegin, exeEnd - exeBegin).trimmed();
  QString args = cmd.mid(exeEnd + 1).trimmed();

  return {std::move(exe), std::move(args)};
}

QString thisProcessPath()
{
  return processPath();
}

QString processFilename(HANDLE process = INVALID_HANDLE_VALUE)
{
  const auto p = processPath(process);
  if (p.isEmpty()) {
    return {};
  }

  return QFileInfo(p).fileName();
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
    std::cerr << "can't get current process filename, defaulting to "
              << defaultName.toStdString() << "\n";

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
  return std::move(
      QStandardPaths::standardLocations(QStandardPaths::TempLocation).first());
}

QString safeVersion()
{
  try {
    // this can throw
    return MOShared::createVersionInfo().string() % u"-"_s;
  } catch (...) {
    return {};
  }
}

int tempFile(const QString& dir)
{
  // maximum tries of incrementing the counter
  const int MaxTries = 100;

  // UTC time and date will be in the filename
  const QDateTime time = QDateTime::currentDateTimeUtc();

  // "ModOrganizer-YYYYMMDDThhmmss.dmp", with a possible "-i" appended, where
  // i can go until MaxTries
  const QString prefix =
      u"ModOrganizer-"_s % safeVersion() % time.toString(u"yyyyMMddThhmmss"_s);
  const QString ext = u".dmp"_s;

  // first path to try, without counter in it
  QString path = dir % "/" % prefix % ext;

  for (int i = 0; i < MaxTries; ++i) {
    std::cout << "trying file '" << path.toStdString() << "'\n";

    if (!QFile::exists(path)) {
      int fd = open(path.toStdString().c_str(), fileFlags, fileMode);
      if (fd == INVALID_HANDLE_VALUE) {
        const auto e = GetLastError();
        // probably no write access
        std::cerr << "failed to create dump file, " << formatSystemMessage(e) << "\n";

        return INVALID_HANDLE_VALUE;
      }
      std::cout << "using file '" << path.toStdString() << "'\n";
      return fd;
    }
    // try again with "-i"
    path = dir % u"/"_s % prefix % u"-"_s % QString::number(i + 1) % ext;
  }

  std::cerr << "can't create dump file, ran out of filenames\n";
  return {};
}

HandlePtr dumpFile(const QString& dir)
{
  // try the given directory, if any
  if (!dir.isEmpty()) {
    int fd = tempFile(dir);
    if (fd != INVALID_HANDLE_VALUE) {
      return HandlePtr(fd);
    }
  }

  // try the current directory
  int fd = tempFile(u"."_s);
  if (fd != INVALID_HANDLE_VALUE) {
    return HandlePtr(fd);
  }

  std::clog << "cannot write dump file in current directory\n";

  // try the temp directory
  const auto temp = tempDir();

  if (!temp.isEmpty()) {
    fd = tempFile(temp);
    if (fd != INVALID_HANDLE_VALUE) {
      return HandlePtr(fd);
    }
  }

  return {};
}

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

bool coredump(const QString& dir, CoreDumpTypes type)
{
  std::wclog << L"creating minidump for the current process\n";
  return createMiniDump(dir, GetCurrentProcess(), type);
}

}  // namespace env
