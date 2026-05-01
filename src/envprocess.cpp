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

DWORD getProcessParentID(HANDLE handle)
{
  return getProcessParentID(GetProcessId(handle));
}

}  // namespace env
