#ifndef ENVPROCESS_H
#define ENVPROCESS_H

#include <vector>

#include "envmodule.h"
#ifdef __unix__
#include <linux/compatibility.h>
#include <linux/fdcloser.h>
#endif

namespace env
{

#ifdef _WIN32
// used by HandlePtr, calls CloseHandle() as the deleter
//
struct HandleCloser
{
  using pointer = HANDLE;

  void operator()(HANDLE h)
  {
    if (h != INVALID_HANDLE_VALUE) {
      ::CloseHandle(h);
    }
  }
};

using HandlePtr = std::unique_ptr<HANDLE, HandleCloser>;
#else
using HandlePtr = FdCloser;
#endif

// represents one process
//
class Process
{
public:
  Process();
  explicit Process(HANDLE h);
  Process(DWORD pid, DWORD ppid, QString name);

  bool isValid() const;
  DWORD pid() const;
  DWORD ppid() const;
  const QString& name() const;

  HandlePtr openHandleForWait() const;

  // whether this process can be accessed; fails if the current process doesn't
  // have the proper permissions
  //
  bool canAccess() const;

  void addChild(Process p);
  std::vector<Process>& children();
  const std::vector<Process>& children() const;

private:
  DWORD m_pid;
  mutable std::optional<DWORD> m_ppid;
  mutable std::optional<QString> m_name;
  std::vector<Process> m_children;
};

std::vector<Process> getRunningProcesses();
std::vector<Module> getLoadedModules();

// works for both jobs and processes
//
Process getProcessTree(HANDLE h);

QString getProcessName(DWORD pid);
QString getProcessName(HANDLE process);

DWORD getProcessParentID(DWORD pid);
DWORD getProcessParentID(HANDLE handle);

}  // namespace env

#endif  // ENVPROCESS_H
