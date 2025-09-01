#include "envprocess.h"
#include "env.h"
#include "envmodule.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

Process::Process() : Process(0, 0, {}) {}

Process::Process(HANDLE h) : Process(::GetProcessId(h), 0, {}) {}

Process::Process(DWORD pid, DWORD ppid, QString name)
    : m_pid(pid), m_ppid(ppid), m_name(std::move(name))
{}

bool Process::isValid() const
{
  return (m_pid != 0);
}

DWORD Process::pid() const
{
  return m_pid;
}

DWORD Process::ppid() const
{
  if (!m_ppid) {
    m_ppid = getProcessParentID(m_pid);
  }

  return *m_ppid;
}

const QString& Process::name() const
{
  if (!m_name) {
    m_name = getProcessName(m_pid);
  }

  return *m_name;
}

void Process::addChild(Process p)
{
  m_children.push_back(p);
}

std::vector<Process>& Process::children()
{
  return m_children;
}

const std::vector<Process>& Process::children() const
{
  return m_children;
}

void findChildren(Process& parent, const std::vector<Process>& processes)
{
  for (auto&& p : processes) {
    if (p.ppid() == parent.pid()) {
      Process child = p;
      findChildren(child, processes);

      parent.addChild(child);
    }
  }
}

Process getProcessTreeFromProcess(HANDLE h)
{
  Process root;

  const auto parentPID = ::GetProcessId(h);
  const auto v         = getRunningProcesses();

  for (auto&& p : v) {
    if (p.pid() == parentPID) {
      Process child = p;
      findChildren(child, v);
      root.addChild(child);
      break;
    }
  }

  return root;
}

void findChildProcesses(Process& parent, std::vector<Process>& processes)
{
  // find all processes that are direct children of `parent`
  auto itor = processes.begin();

  while (itor != processes.end()) {
    if (itor->ppid() == parent.pid()) {
      parent.addChild(*itor);
      itor = processes.erase(itor);
    } else {
      ++itor;
    }
  }

  // find all processes that are direct children of `parent`'s children
  for (auto&& c : parent.children()) {
    findChildProcesses(c, processes);
  }
}

DWORD getProcessParentID(HANDLE handle)
{
  return getProcessParentID(GetProcessId(handle));
}

}  // namespace env
