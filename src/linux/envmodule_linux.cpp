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
VS_FIXEDFILEINFO Module::getFixedFileInfo(std::byte* buffer) const{
  STUB();
  return {};
}

QString Module::getFileDescription(std::byte* buffer) const{
  STUB();
  return {};
}


bool Module::interesting() const
{
  STUB();
  return {};
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
  fs::perms p = filesystem::status(statusPath).permissions();
  return p == fs::perms::others_read;
}

std::vector<Process> getRunningProcesses()
{
  std::vector<Process> v;
  fs::directory_iterator it("/proc");

  for(auto folder :  it) {
    v.emplace_back(stoi(folder.path().filename()));
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
  ifstream comm("/proc/" + to_string(pid) + "/comm");
  if(!comm.is_open()) {
    log::error("error reading process info for pid {}", pid);
  }
  string name;
  comm >> name;
  return QString::fromUtf8(name);
}

DWORD getProcessParentID(DWORD pid)
{
  // see proc_pid_stat(5) manpage for documentation

  // QFile can't be used because it can't handle virtual files
  ifstream stat(format("/proc/{}/stat", pid));
  stat.exceptions(std::ios::failbit);
  try {
    int readPid;
    string comm;
    char state;
    int ppid;

    stat >> readPid >> comm >> state >> ppid;

    return ppid;
  } catch (const std::exception & ex) {
    log::warn("could not get ppid of pid {}: {}", pid, ex.what());
    return 0;
  }
}

}  // namespace env
