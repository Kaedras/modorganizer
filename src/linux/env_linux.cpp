#include "env.h"
#include "envdump.h"
#include "envmodule.h"
#include "envprocess.h"
#include "shared/util.h"
#include "stub.h"
#include <linux/compatibility.h>
#include <utility.h>

// fixes an error message in google breakpad
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <breakpad/client/linux/handler/exception_handler.h>
#include <breakpad/client/linux/minidump_writer/minidump_writer.h>

using namespace Qt::StringLiterals;

namespace env
{

using namespace MOBase;

extern DWORD findOtherPid();

Console::Console() : m_hasConsole(true), m_in(stdin), m_out(stdout), m_err(stderr) {}

Console::~Console() {}

ModuleNotification::~ModuleNotification() {}

std::optional<QString> getAssocString(const QFileInfo& file)
{
  return QStringLiteral("xdg-open %1").arg(file.absoluteFilePath());
}

QString processPath(HANDLE process = INVALID_HANDLE_VALUE)
{
  if (process == 0) {
    return {};
  }

  pid_t pid;
  if (process == INVALID_HANDLE_VALUE) {
    pid = getpid();
  } else {
    pid = pidfd_getpid(process);
    if (pid == -1) {
      return {};
    }
  }

  std::filesystem::path exe("/proc/" + std::to_string(pid) + "/exe");
  return QString::fromStdString(read_symlink(exe));
}

std::unique_ptr<ModuleNotification>
Environment::onModuleLoaded(QObject* o, std::function<void(Module)> f)
{
  return std::make_unique<ModuleNotification>(o, f);
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
  const auto cmd = getAssocString(targetInfo);

  log::debug("raw cmd is '{}'", *cmd);

  return {QFileInfo(u"xdg-open"_s),
          QStringLiteral("xdg-open %1").arg(targetInfo.absoluteFilePath()),
          targetInfo.absoluteFilePath()};
}

bool registryValueExists(const QString&, const QString&)
{
  return false;
}

void deleteRegistryKeyIfEmpty(const QString&) {}

QString thisProcessPath()
{
  pid_t pid = getpid();

  std::filesystem::path exe("/proc/" + std::to_string(pid) + "/exe");
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
      u"ModOrganizer-"_s % safeVersion() % time.toString(u"yyyyMMddThhmmss"_s);
  const QString ext = u".dmp"_s;

  // first path to try, without counter in it
  QString path = dir % u"/"_s % prefix % ext;

  for (int i = 0; i < MaxTries; ++i) {
    std::clog << "trying file '" << path.toStdString() << "'\n";

    if (!QFile::exists(path)) {
      int fd = open(path.toStdString().c_str(), O_WRONLY | O_CREAT | O_EXCL,
                    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
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

bool createMiniDumpForPid(const QString& dir, pid_t process, CoreDumpTypes type)
{
  std::string dumpPath;

  HandlePtr file = dumpFile(dir);
  if (!file) {
    std::cerr << "nowhere to write the dump file\n";
    return false;
  }

  using namespace google_breakpad;

  ExceptionHandler::CrashContext blob{{}, process};
  bool result = WriteMinidump(file.get(), process, &blob, sizeof(blob));
  if (!result) {
    const int e = errno;
    std::cerr << "Error creating minidump, " << strerror(e) << "\n";
  }

  return result;
}

bool createMiniDump(const QString& dir, HANDLE process, CoreDumpTypes type)
{
  pid_t target = pidfd_getpid(process);
  if (target == -1) {
    const int e = errno;
    std::cerr << "Error getting pid from pidfd, " << strerror(e) << "\n";
    return false;
  }

  return createMiniDumpForPid(dir, target, type);
}

bool coredumpOther(CoreDumpTypes type)
{
  std::cout << "creating minidump for a running process\n";
  const pid_t pid = findOtherPid();
  if (pid == 0) {
    std::cerr << "no other process found\n";
    return false;
  }

  std::cout << "found other process with pid " << pid << "\n";

  return createMiniDumpForPid(nullptr, pid, type);
}

}  // namespace env
