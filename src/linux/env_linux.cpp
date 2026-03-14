#include "env.h"
#include "envdump.h"
#include "envmodule.h"
#include "envprocess.h"
#include "shared/util.h"
#include "stub.h"

using namespace Qt::StringLiterals;
using namespace std;
namespace fs = std::filesystem;

namespace env
{

using namespace MOBase;

extern DWORD findOtherPid();

Console::Console() : m_hasConsole(true), m_in(stdin), m_out(stdout), m_err(stderr) {}

Console::~Console() {}

ModuleNotification::~ModuleNotification() {}

std::optional<QString> getAssocString(const QFileInfo& file)
{
  auto xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
  if (xdgOpen.isEmpty()) {
    return {};
  }
  return xdgOpen % " \""_L1 % file.absoluteFilePath() % "\""_L1;
}

QString processPath(HANDLE process = INVALID_HANDLE_VALUE)
{
  if (process == 0) {
    return {};
  }

  string pidStr;
  if (process == INVALID_HANDLE_VALUE) {
    pidStr = "self";
  } else {
    pid_t pid = pidfd_getpid(process);
    if (pid == -1) {
      return {};
    }
    pidStr = to_string(pid);
  }

  fs::path exe("/proc/" + pidStr + "/exe");
  return QString::fromStdString(read_symlink(exe));
}

std::unique_ptr<ModuleNotification>
Environment::onModuleLoaded(QObject* o, std::function<void(Module)> f)
{
  return make_unique<ModuleNotification>(o, f);
}

QString get(const QString& name)
{
  return qEnvironmentVariable(name.toStdString().c_str());
}

void set(const QString& n, const QString& v)
{
  if (v.isEmpty()) {
    qunsetenv(n.toStdString().c_str());
  } else {
    qputenv(n.toStdString().c_str(), v.toLocal8Bit());
  }
}

Association getAssociation(const QFileInfo& targetInfo)
{
  auto cmd = getAssocString(targetInfo);

  log::debug("raw cmd is '{}'", *cmd);

  auto xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));

  return {QFileInfo(xdgOpen), *cmd, "\""_L1 % targetInfo.absoluteFilePath() % "\""_L1};
}

bool registryValueExists(const QString&, const QString&)
{
  // no-op
  return false;
}

void deleteRegistryKeyIfEmpty(const QString&)
{
  // no-op
}

QString thisProcessPath()
{
  fs::path exe("/proc/self/exe");
  return QFileInfo(read_symlink(exe)).path();
}

QString safeVersion()
{
  try {
    // this can throw
    return MOShared::createVersionInfo().string() % "-"_L1;
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
      "ModOrganizer-"_L1 % safeVersion() % time.toString(u"yyyyMMddThhmmss"_s);
  const QString ext = u".dmp"_s;

  // first path to try, without counter in it
  QString path = dir % "/"_L1 % prefix % ext;

  for (int i = 0; i < MaxTries; ++i) {
    clog << "trying file '" << path.toStdString() << "'\n";

    if (!QFile::exists(path)) {
      int fd = open(path.toStdString().c_str(), O_WRONLY | O_CREAT | O_EXCL,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
      if (fd == INVALID_HANDLE_VALUE) {
        const auto e = GetLastError();
        // probably no write access
        cerr << "failed to create dump file, " << formatSystemMessage(e) << "\n";

        return INVALID_HANDLE_VALUE;
      }
      cout << "using file '" << path.toStdString() << "'\n";
      return fd;
    }
    // try again with "-i"
    path = dir % "/"_L1 % prefix % "-"_L1 % QString::number(i + 1) % ext;
  }

  cerr << "can't create dump file, ran out of filenames\n";
  return {};
}

QString tempDir()
{
  return QStandardPaths::standardLocations(QStandardPaths::TempLocation).first();
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

  clog << "cannot write dump file in current directory\n";

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

bool createMiniDumpForPid(const QString& dir, pid_t process, CoreDumpTypes type)
{
  string dumpPath;

  HandlePtr file = dumpFile(dir);
  if (!file) {
    cerr << "nowhere to write the dump file\n";
    return false;
  }

  using namespace google_breakpad;

  ExceptionHandler::CrashContext blob{{}, process};
  bool result = WriteMinidump(file.get(), process, &blob, sizeof(blob));
  if (!result) {
    const int e = errno;
    cerr << "Error creating minidump, " << strerror(e) << "\n";
  }

  return result;
}

bool createMiniDump(const QString& dir, HANDLE process, CoreDumpTypes type)
{
  pid_t target = pidfd_getpid(process);
  if (target == -1) {
    const int e = errno;
    cerr << "Error getting pid from pidfd, " << strerror(e) << "\n";
    return false;
  }

  return createMiniDumpForPid(dir, target, type);
}

bool coredumpOther(CoreDumpTypes type)
{
  cout << "creating minidump for a running process\n";
  const pid_t pid = findOtherPid();
  if (pid == 0) {
    cerr << "no other process found\n";
    return false;
  }

  cout << "found other process with pid " << pid << "\n";

  return createMiniDumpForPid(nullptr, pid, type);
}

}  // namespace env
