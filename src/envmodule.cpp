#include "envmodule.h"
#include "env.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

// the rationale for logging md5 was to make sure the various files were the
// same as in the released version; this turned out to be of dubious interest,
// while adding to the startup time
constexpr bool UseMD5 = false;

Module::Module(QString path, std::size_t fileSize)
    : m_path(std::move(path)), m_fileSize(fileSize)
{
  const auto fi = getFileInfo();

  m_version       = getVersion(fi.ffi);
  m_timestamp     = getTimestamp(fi.ffi);
  m_versionString = fi.fileDescription;

  if (UseMD5) {
    m_md5 = getMD5();
  }
}

const QString& Module::path() const
{
  return m_path;
}

QString Module::displayPath() const
{
  return QDir::fromNativeSeparators(m_path.toLower());
}

std::size_t Module::fileSize() const
{
  return m_fileSize;
}

const QString& Module::version() const
{
  return m_version;
}

const QString& Module::versionString() const
{
  return m_versionString;
}

const QDateTime& Module::timestamp() const
{
  return m_timestamp;
}

const QString& Module::md5() const
{
  return m_md5;
}

QString Module::timestampString() const
{
  if (!m_timestamp.isValid()) {
    return "(no timestamp)";
  }

  return m_timestamp.toString(Qt::DateFormat::ISODate);
}

QString Module::toString() const
{
  QStringList sl;

  // file size
  sl.push_back(displayPath());
  sl.push_back(QString("%1 B").arg(m_fileSize));

  // version
  if (m_version.isEmpty() && m_versionString.isEmpty()) {
    sl.push_back("(no version)");
  } else {
    if (!m_version.isEmpty()) {
      sl.push_back(m_version);
    }

    if (!m_versionString.isEmpty() && m_versionString != m_version) {
      sl.push_back(versionString());
    }
  }

  // timestamp
  if (m_timestamp.isValid()) {
    sl.push_back(m_timestamp.toString(Qt::DateFormat::ISODate));
  } else {
    sl.push_back("(no timestamp)");
  }

  // md5
  if (!m_md5.isEmpty()) {
    sl.push_back(m_md5);
  }

  return sl.join(", ");
}

QString Module::getMD5() const
{
  static const std::set<QString> ignore = {
      "\\windows\\", "\\program files\\", "\\program files (x86)\\", "\\programdata\\"};

  // don't calculate md5 for system files, it's not really relevant and
  // it takes a while
  for (auto&& i : ignore) {
    if (m_path.contains(i, Qt::CaseInsensitive)) {
      return {};
    }
  }

  // opening the file
  QFile f(m_path);

  if (!f.open(QFile::ReadOnly)) {
    log::error("failed to open file '{}' for md5", m_path);
    return {};
  }

  // hashing
  QCryptographicHash hash(QCryptographicHash::Md5);
  if (!hash.addData(&f)) {
    log::error("failed to calculate md5 for '{}'", m_path);
    return {};
  }

  return hash.result().toHex();
}

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

}  // namespace env
