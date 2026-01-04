#include "envprocess.h"

#include <QFile>
#include <QFileInfo>
#include <log.h>

using namespace std;
using namespace Qt::StringLiterals;
namespace fs = std::filesystem;

namespace env
{

using namespace MOBase;

extern Process getProcessTreeFromProcess(HANDLE h);

HandlePtr Process::openHandleForWait() const
{
  HandlePtr h = pidfd_open((pid_t)m_pid, PIDFD_NONBLOCK);
  return h;
}

// whether this process can be accessed; fails if the current process doesn't
// have the proper permissions
//
bool Process::canAccess() const
{
  HandlePtr h = pidfd_open((pid_t)m_pid, 0);
  if (!h) {
    return false;
  }
  return true;
}

std::vector<Module> getLoadedModules()
{
  std::vector<Module> v;

  QFile maps(u"/proc/self/maps"_s);
  if (!maps.open(QIODevice::ReadOnly)) {
    log::error("error reading /proc/self/maps, {}", maps.errorString());
    return {};
  }

  // parse file
  // sample line:
  // 7fc504894000-7fc5048bc000 r--p 00000000 00:1b 129777595   /usr/lib64/libc.so.6
  QStringList files;
  QTextStream stream(&maps);
  QString line;
  while (stream.readLineInto(&line)) {
    if (line.contains(".so"_L1)) {
      auto pos = line.indexOf('/');
      line     = line.sliced(pos);
      files << line.trimmed();
    }
  }
  files.removeDuplicates();

  for (const auto& file : files) {
    v.emplace_back(file, QFileInfo(file).size());
  }

  // sorting by display name
  ranges::sort(v, [](auto&& a, auto&& b) {
    return a.displayPath().compare(b.displayPath(), Qt::CaseInsensitive) < 0;
  });

  return v;
}

std::vector<Process> getRunningProcesses()
{
  vector<Process> v;
  fs::directory_iterator it("/proc");
  try {
    for (const auto& item : it) {
      if (!item.is_directory()) {
        continue;
      }
      try {
        // directory name may not be an integer, in which case it can be ignored
        pid_t pid = stoi(item.path().filename().string());

        // common errors
        // EACCES: the process probably belongs to another user
        // ENOENT: the process has exited before this function has finished

        auto procName = getProcessName(static_cast<uint32_t>(pid));

        // ignore the process
        if (procName.isEmpty()) {
          const int e = errno;
          if (e != EACCES && e != ENOENT) {
            log::warn("error getting process name for pid {}, {}", pid, strerror(e));
          }
          continue;
        }

        char state;
        pid_t ppid;

        FILE* file = fopen(format("/proc/{}/stat", pid).c_str(), "r");
        if (file == nullptr) {
          const int e = errno;
          if (e != EACCES && e != ENOENT) {
            log::warn("error opening stats for pid {}, {}", pid, strerror(e));
          }
          continue;
        }
        int itemsRead = fscanf(file, "%*u (%*[^)]%*[)] %c %d", &state, &ppid);
        fclose(file);
        if (itemsRead != 2) {
          if (errno != EACCES) {
            log::warn("error reading stats for pid {}, {}", pid, strerror(errno));
          }
          continue;
        }

        // don't include zombies and dead processes
        if (state == 'Z' || state == 'X') {
          continue;
        }

        Process p(pid, ppid, std::move(procName));
        v.push_back(p);
      } catch (const invalid_argument&) {
      }
    }
  } catch (const fs::filesystem_error& e) {
    log::error("error while iterating over '/proc', {}", e.what());
  }

  return v;
}

Process getProcessTree(HANDLE h)
{
  return getProcessTreeFromProcess(h);
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
  fclose(file);
  if (items != 1) {
    const int error = errno;
    log::warn("could not get ppid of pid {}: {}", pid, strerror(error));
    return 0;
  }
  return ppid;
}

}  // namespace env
