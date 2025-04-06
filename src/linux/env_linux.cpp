#include "compatibility.h"
#include "env.h"
#include "envdump.h"
#include "envmetrics.h"
#include "envmodule.h"
#include "envshortcut.h"
#include "settings.h"
#include "shared/util.h"
#include "stub.h"
#include <log.h>
#include <sys/prctl.h>
#include <utility.h>

// fixes an error message in breakpad
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <client/linux/handler/exception_handler.h>
#include <client/linux/minidump_writer/minidump_writer.h>

using namespace std;
using namespace Qt::StringLiterals;

namespace env
{

using namespace MOBase;

extern DWORD findOtherPid();
extern HandlePtr dumpFile(const QString& dir);
extern QString dumpFileName(const QString& dir);

Console::Console() : m_hasConsole(true), m_in(stdin), m_out(stdout), m_err(stderr) {}

Console::~Console() {}

ModuleNotification::~ModuleNotification() {}

std::unique_ptr<ModuleNotification>
Environment::onModuleLoaded(QObject* o, std::function<void(Module)> f)
{
  return std::make_unique<ModuleNotification>(o, f);
}

std::optional<QString> getAssocString(const QFileInfo& file)
{
  STUB_PARAM(file.absolutePath().toStdString());
  return {};
  // const auto ext = "."s + file.suffix().toStdString();
}

// returns the filename of the given process or the current one
//
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

Association getAssociation(const QFileInfo& targetInfo)
{
  STUB_PARAM(targetInfo.absolutePath().toStdString());
  return {};
}

bool registryValueExists([[maybe_unused]] const QString& key,
                         [[maybe_unused]] const QString& value)
{
  return false;
}

void deleteRegistryKeyIfEmpty([[maybe_unused]] const QString& name) {}

bool createMiniDumpForPid(const QString& dir, pid_t process, CoreDumpTypes type)
{
  string dumpPath;

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
