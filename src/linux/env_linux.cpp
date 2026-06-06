#include "env.h"
#include "envdump.h"
#include "envmodule.h"
#include "envprocess.h"
#include "shared/util.h"

#include <QStandardPaths>
#include <client/linux/handler/exception_handler.h>
#include <client/linux/minidump_writer/minidump_writer.h>
#include <iostream>
#include <utility.h>

using namespace Qt::StringLiterals;
using namespace std;
namespace fs = std::filesystem;

namespace env
{

using namespace MOBase;

// functions are defined in `env.cpp`
DWORD findOtherPid();
std::unique_ptr<QFile> dumpFile(const QString& dir);

Console::Console() : m_hasConsole(true), m_in(stdin), m_out(stdout), m_err(stderr) {}

Console::~Console() {}

ModuleNotification::~ModuleNotification() {}

std::optional<QString> getAssocString(const QFileInfo& file)
{
  auto xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
  if (xdgOpen.isEmpty()) {
    return {};
  }
  return xdgOpen % " \""_L1 % file.absoluteFilePath() % '\"';
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

Association getAssociation(const QFileInfo& targetInfo)
{
  auto cmd = getAssocString(targetInfo);

  log::debug("raw cmd is '{}'", *cmd);

  auto xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));

  return {QFileInfo(xdgOpen), *cmd, '\"' % targetInfo.absoluteFilePath() % '\"'};
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

bool createMiniDumpForPid(const QString& dir, pid_t process, CoreDumpTypes type)
{
  string dumpPath;

  std::unique_ptr<QFile> file = dumpFile(dir);
  if (!file) {
    cerr << "nowhere to write the dump file\n";
    return false;
  }

  using namespace google_breakpad;

  ExceptionHandler::CrashContext blob{{}, process};
  bool result = WriteMinidump(file->handle(), process, &blob, sizeof(blob));
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
