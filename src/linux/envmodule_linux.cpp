#include "env.h"
#include "envmodule.h"
#include "stub.h"

#include <fstream>
#include <log.h>
#include <utility.h>

using namespace std;
namespace fs = std::filesystem;

namespace env
{

using namespace MOBase;

Module::FileInfo Module::getFileInfo() const
{
  STUB();
  return {};
}
QString Module::getVersion(const VS_FIXEDFILEINFO& fi) const
{
  STUB();
  return {};
}
QDateTime Module::getTimestamp(const VS_FIXEDFILEINFO& fi) const
{
  STUB();
  return {};
}
VS_FIXEDFILEINFO Module::getFixedFileInfo(std::byte* buffer) const
{
  STUB();
  return {};
}

QString Module::getFileDescription(std::byte* buffer) const
{
  STUB();
  return {};
}

bool Module::interesting() const
{
  STUB();
  return false;
}

HandlePtr Process::openHandleForWait() const
{
  STUB();
  return HandlePtr{-1};
}

// whether this process can be accessed; fails if the current process doesn't
// have the proper permissions
//
bool Process::canAccess() const
{
  fs::path statusPath = "/proc/" + to_string(m_pid) + "/status";
  fs::perms p         = filesystem::status(statusPath).permissions();
  return (p & fs::perms::others_read) != fs::perms::none;
}

std::vector<Process> getRunningProcesses()
{
  std::vector<Process> v;
  fs::directory_iterator it("/proc");

  for (const auto& item : it) {
    if (!item.is_directory()) {
      continue;
    }
    try {
      // directory name may not be an integer, in which case it can be ignored
      pid_t pid = stoi(item.path().filename());
      v.emplace_back(pid, getProcessParentID((DWORD)pid), getProcessName((DWORD)pid));
    } catch (const std::invalid_argument&) {
    }
  }

  return v;
}

std::vector<Module> getLoadedModules()
{
  STUB();
  return {};
}

Process getProcessTree(HANDLE h)
{
  STUB();
  return {};
}

QString getProcessName(DWORD pid)
{
  error_code ec;
  auto path = filesystem::read_symlink("/proc/" + to_string(pid) + "/exe", ec);
  if (ec) {
    return {};
  }
  return QFileInfo(path).fileName();
}

DWORD getProcessParentID(DWORD pid)
{
  // see proc_pid_stat(5) manpage for documentation
  int ppid;

  FILE* file = fopen(format("/proc/{}/stat", pid).c_str(), "r");
  if (file == nullptr) {
    const int error = errno;
    log::warn("could not get ppid of pid {}: {}", pid, strerror(error));
    return 0;
  }
  int items = fscanf(file, "%*u (%*[^)]%*[)] %*c %d", &ppid);
  if (items != 1) {
    const int error = errno;
    log::warn("could not get ppid of pid {}: {}", pid, strerror(error));
    return 0;
  }
  return ppid;
}

}  // namespace env
