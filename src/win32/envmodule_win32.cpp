#include "envmodule.h"
#include "env.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;



bool isJobHandle(HANDLE h)
{
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info = {};

  const auto r = ::QueryInformationJobObject(h, JobObjectBasicAccountingInformation,
                                             &info, sizeof(info), nullptr);

  return r;
}

Process getProcessTree(HANDLE h)
{
  if (isJobHandle(h)) {
    return getProcessTreeFromJob(h);
  } else {
    return getProcessTreeFromProcess(h);
  }
}

QString getProcessName(DWORD pid)
{
  HandlePtr h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));

  if (!h) {
    const auto e = GetLastError();
    log::error("can't get name of process {}, {}", pid, formatSystemMessage(e));
    return {};
  }

  return getProcessName(h.get());
}

QString getProcessName(HANDLE process)
{
  const QString badName = "unknown";

  if (process == 0 || process == INVALID_HANDLE_VALUE) {
    return badName;
  }

  const DWORD bufferSize         = MAX_PATH;
  wchar_t buffer[bufferSize + 1] = {};

  const auto realSize = ::GetProcessImageFileNameW(process, buffer, bufferSize);

  if (realSize == 0) {
    const auto e = ::GetLastError();
    log::error("GetProcessImageFileNameW() failed, {}", formatSystemMessage(e));
    return badName;
  }

  auto s = QString::fromWCharArray(buffer, realSize);

  const auto lastSlash = s.lastIndexOf("\\");
  if (lastSlash != -1) {
    s = s.mid(lastSlash + 1);
  }

  return s;
}


Module::FileInfo Module::getFileInfo() const
{
  const auto wspath = m_path.toStdWString();

  // getting version info size
  DWORD dummy      = 0;
  const DWORD size = GetFileVersionInfoSizeW(wspath.c_str(), &dummy);

  if (size == 0) {
    const auto e = GetLastError();

    if (e == ERROR_RESOURCE_TYPE_NOT_FOUND) {
      // not an error, no version information built into that module
      return {};
    }

    if (e == ERROR_RESOURCE_DATA_NOT_FOUND) {
      // not an error, no version information built into that module;
      // happens often in wine
      return {};
    }

    log::debug("GetFileVersionInfoSizeW() failed on '{}', {}", m_path,
               formatSystemMessage(e));

    return {};
  }

  // getting version info
  auto buffer = std::make_unique<std::byte[]>(size);

  if (!GetFileVersionInfoW(wspath.c_str(), 0, size, buffer.get())) {
    const auto e = GetLastError();

    log::error("GetFileVersionInfoW() failed on '{}', {}", m_path,
               formatSystemMessage(e));

    return {};
  }

  // the version info has two major parts: a fixed version and a localizable
  // set of strings

  FileInfo fi;
  fi.ffi             = getFixedFileInfo(buffer.get());
  fi.fileDescription = getFileDescription(buffer.get());

  return fi;
}

VS_FIXEDFILEINFO Module::getFixedFileInfo(std::byte* buffer) const
{
  void* valuePointer     = nullptr;
  unsigned int valueSize = 0;

  // the fixed version info is in the root
  const auto ret = VerQueryValueW(buffer, L"\\", &valuePointer, &valueSize);

  if (!ret || !valuePointer || valueSize == 0) {
    // not an error, no fixed file info
    return {};
  }

  const auto* fi = static_cast<VS_FIXEDFILEINFO*>(valuePointer);

  // signature is always 0xfeef04bd
  if (fi->dwSignature != 0xfeef04bd) {
    log::error("bad file info signature {:#x} for '{}'", fi->dwSignature, m_path);

    return {};
  }

  return *fi;
}

QString Module::getFileDescription(std::byte* buffer) const
{
  struct LANGANDCODEPAGE
  {
    WORD wLanguage;
    WORD wCodePage;
  };

  void* valuePointer     = nullptr;
  unsigned int valueSize = 0;

  // getting list of available languages
  auto ret =
      VerQueryValueW(buffer, L"\\VarFileInfo\\Translation", &valuePointer, &valueSize);

  if (!ret || !valuePointer || valueSize == 0) {
    log::error("VerQueryValueW() for translations failed on '{}'", m_path);
    return {};
  }

  // number of languages
  const auto count = valueSize / sizeof(LANGANDCODEPAGE);
  if (count == 0) {
    return {};
  }

  // using the first language in the list to get FileVersion
  const auto* lcp = static_cast<LANGANDCODEPAGE*>(valuePointer);

  const auto subBlock = QString("\\StringFileInfo\\%1%2\\FileVersion")
                            .arg(lcp->wLanguage, 4, 16, QChar('0'))
                            .arg(lcp->wCodePage, 4, 16, QChar('0'));

  ret = VerQueryValueW(buffer, subBlock.toStdWString().c_str(), &valuePointer,
                       &valueSize);

  if (!ret || !valuePointer || valueSize == 0) {
    // not an error, no file version
    return {};
  }

  // valueSize includes the null terminator
  return QString::fromWCharArray(static_cast<wchar_t*>(valuePointer), valueSize - 1);
}


QString Module::getVersion(const VS_FIXEDFILEINFO& fi) const
{
  if (fi.dwSignature == 0) {
    return {};
  }

  const DWORD major       = (fi.dwFileVersionMS >> 16) & 0xffff;
  const DWORD minor       = (fi.dwFileVersionMS >> 0) & 0xffff;
  const DWORD maintenance = (fi.dwFileVersionLS >> 16) & 0xffff;
  const DWORD build       = (fi.dwFileVersionLS >> 0) & 0xffff;

  if (major == 0 && minor == 0 && maintenance == 0 && build == 0) {
    return {};
  }

  return QString("%1.%2.%3.%4").arg(major).arg(minor).arg(maintenance).arg(build);
}

QDateTime Module::getTimestamp(const VS_FIXEDFILEINFO& fi) const
{
  FILETIME ft = {};

  if (fi.dwSignature == 0 || (fi.dwFileDateMS == 0 && fi.dwFileDateLS == 0)) {
    // if the file info is invalid or doesn't have a date, use the creation
    // time on the file

    // opening the file
    HandlePtr h(CreateFileW(m_path.toStdWString().c_str(), GENERIC_READ,
                            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, 0));

    if (h.get() == INVALID_HANDLE_VALUE) {
      const auto e = GetLastError();

      log::debug("can't open file '{}' for timestamp, {}", m_path,
                 formatSystemMessage(e));

      return {};
    }

    // getting the file time
    if (!GetFileTime(h.get(), &ft, nullptr, nullptr)) {
      const auto e = GetLastError();

      log::error("can't get file time for '{}', {}", m_path, formatSystemMessage(e));

      return {};
    }
  } else {
    // use the time from the file info
    ft.dwHighDateTime = fi.dwFileDateMS;
    ft.dwLowDateTime  = fi.dwFileDateLS;
  }

  // converting to SYSTEMTIME
  SYSTEMTIME utc = {};

  if (!FileTimeToSystemTime(&ft, &utc)) {
    log::error(
        "FileTimeToSystemTime() failed on timestamp high={:#x} low={:#x} for '{}'",
        ft.dwHighDateTime, ft.dwLowDateTime, m_path);

    return {};
  }

  return QDateTime(QDate(utc.wYear, utc.wMonth, utc.wDay),
                   QTime(utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds));
}

bool Module::interesting() const
{
  static const auto windir = []() -> QString {
    try {
      return QDir::toNativeSeparators(MOBase::getKnownFolder(FOLDERID_Windows).path()) +
             "\\";
    } catch (...) {
      return "c:\\windows\\";
    }
  }();

  if (m_path.startsWith(windir, Qt::CaseInsensitive)) {
    return false;
  }

  return true;
}


HandlePtr Process::openHandleForWait() const
{
  const auto rights =
      PROCESS_QUERY_LIMITED_INFORMATION |     // exit code, image name, etc.
      SYNCHRONIZE |                           // wait functions
      PROCESS_SET_QUOTA | PROCESS_TERMINATE;  // add to job

  // don't log errors, failure can happen if the process doesn't exist
  return HandlePtr(OpenProcess(rights, FALSE, m_pid));
}

// whether this process can be accessed; fails if the current process doesn't
// have the proper permissions
//
bool Process::canAccess() const
{
  HandlePtr h(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, m_pid));

  if (!h) {
    const auto e = GetLastError();
    if (e == ERROR_ACCESS_DENIED) {
      return false;
    }
  }

  return true;
}


std::vector<Module> getLoadedModules()
{
  HandlePtr snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE32 | TH32CS_SNAPMODULE,
                                              GetCurrentProcessId()));

  if (snapshot.get() == INVALID_HANDLE_VALUE) {
    const auto e = GetLastError();
    log::error("CreateToolhelp32Snapshot() failed, {}", formatSystemMessage(e));
    return {};
  }

  MODULEENTRY32 me = {};
  me.dwSize        = sizeof(me);

  // first module, this shouldn't fail because there's at least the executable
  if (!Module32First(snapshot.get(), &me)) {
    const auto e = GetLastError();
    log::error("Module32First() failed, {}", formatSystemMessage(e));
    return {};
  }

  std::vector<Module> v;

  for (;;) {
    const auto path = QString::fromWCharArray(me.szExePath);
    if (!path.isEmpty()) {
      v.push_back(Module(path, me.modBaseSize));
    }

    // next module
    if (!Module32Next(snapshot.get(), &me)) {
      const auto e = GetLastError();

      // no more modules is not an error
      if (e != ERROR_NO_MORE_FILES) {
        log::error("Module32Next() failed, {}", formatSystemMessage(e));
      }

      break;
    }
  }

  // sorting by display name
  std::sort(v.begin(), v.end(), [](auto&& a, auto&& b) {
    return (a.displayPath().compare(b.displayPath(), Qt::CaseInsensitive) < 0);
  });

  return v;
}

template <class F>
void forEachRunningProcess(F&& f)
{
  HandlePtr snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));

  if (snapshot.get() == INVALID_HANDLE_VALUE) {
    const auto e = GetLastError();
    log::error("CreateToolhelp32Snapshot() failed, {}", formatSystemMessage(e));
    return;
  }

  PROCESSENTRY32 entry = {};
  entry.dwSize         = sizeof(entry);

  // first process, this shouldn't fail because there's at least one process
  // running
  if (!Process32First(snapshot.get(), &entry)) {
    const auto e = GetLastError();
    log::error("Process32First() failed, {}", formatSystemMessage(e));
    return;
  }

  for (;;) {
    if (!f(entry)) {
      break;
    }

    // next process
    if (!Process32Next(snapshot.get(), &entry)) {
      const auto e = GetLastError();

      // no more processes is not an error
      if (e != ERROR_NO_MORE_FILES)
        log::error("Process32Next() failed, {}", formatSystemMessage(e));

      break;
    }
  }
}


std::vector<Process> getRunningProcesses()
{
  std::vector<Process> v;

  forEachRunningProcess([&](auto&& entry) {
    v.push_back(Process(entry.th32ProcessID, entry.th32ParentProcessID,
                        QString::fromStdWString(entry.szExeFile)));

    return true;
  });

  return v;
}


std::vector<DWORD> processesInJob(HANDLE h)
{
  const int MaxTries = 5;

  // doubled MaxTries times on failure
  DWORD maxIds = 100;

  // for logging
  DWORD lastCount = 0, lastAssigned = 0;

  for (int tries = 0; tries < MaxTries; ++tries) {
    const DWORD idsSize    = sizeof(ULONG_PTR) * maxIds;
    const DWORD bufferSize = sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + idsSize;

    MallocPtr<void> buffer(std::malloc(bufferSize));
    auto* ids = static_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(buffer.get());

    const auto r = QueryInformationJobObject(h, JobObjectBasicProcessIdList, ids,
                                             bufferSize, nullptr);

    if (!r) {
      const auto e = GetLastError();
      if (e != ERROR_MORE_DATA) {
        log::error("failed to get process ids in job, {}", formatSystemMessage(e));
        return {};
      }
    }

    if (ids->NumberOfProcessIdsInList >= ids->NumberOfAssignedProcesses) {
      std::vector<DWORD> v;
      for (DWORD i = 0; i < ids->NumberOfProcessIdsInList; ++i) {
        v.push_back(ids->ProcessIdList[i]);
      }

      return v;
    }

    // try again with a larger buffer
    maxIds *= 2;

    // for logging
    lastCount    = ids->NumberOfProcessIdsInList;
    lastAssigned = ids->NumberOfAssignedProcesses;
  }

  log::error("failed to get processes in job, can't get a buffer large enough, "
             "{}/{} ids",
             lastCount, lastAssigned);

  return {};
}


Process getProcessTreeFromJob(HANDLE h)
{
  const auto ids = processesInJob(h);
  if (ids.empty()) {
    return {};
  }

  std::vector<Process> ps;

  forEachRunningProcess([&](auto&& entry) {
    for (auto&& id : ids) {
      if (entry.th32ProcessID == id) {
        ps.push_back(Process(entry.th32ProcessID, entry.th32ParentProcessID,
                             QString::fromStdWString(entry.szExeFile)));

        break;
      }
    }

    return true;
  });

  Process root;

  {
    // getting processes whose parent is not in the list
    for (auto&& possibleRoot : ps) {
      const auto ppid = possibleRoot.ppid();
      bool found      = false;

      for (auto&& p : ps) {
        if (p.pid() == ppid) {
          found = true;
          break;
        }
      }

      if (!found) {
        // this is a root process
        root.addChild(possibleRoot);
      }
    }

    // removing root processes from the list
    auto newEnd = std::remove_if(ps.begin(), ps.end(), [&](auto&& p) {
      for (auto&& rp : root.children()) {
        if (rp.pid() == p.pid()) {
          return true;
        }
      }

      return false;
    });

    ps.erase(newEnd, ps.end());
  }

  // at this point, `processes` should only contain processes that are direct
  // or indirect children of the ones in `root`

  if (ps.empty()) {
    // and that's all there is
    return root;
  }

  {
    // recursively find children
    for (auto&& r : root.children()) {
      findChildProcesses(r, ps);
    }
  }

  return root;
}

bool isJobHandle(HANDLE h)
{
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info = {};

  const auto r = ::QueryInformationJobObject(h, JobObjectBasicAccountingInformation,
                                             &info, sizeof(info), nullptr);

  return r;
}

Process getProcessTree(HANDLE h)
{
  if (isJobHandle(h)) {
    return getProcessTreeFromJob(h);
  } else {
    return getProcessTreeFromProcess(h);
  }
}

QString getProcessName(DWORD pid)
{
  HandlePtr h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));

  if (!h) {
    const auto e = GetLastError();
    log::error("can't get name of process {}, {}", pid, formatSystemMessage(e));
    return {};
  }

  return getProcessName(h.get());
}

QString getProcessName(HANDLE process)
{
  const QString badName = "unknown";

  if (process == 0 || process == INVALID_HANDLE_VALUE) {
    return badName;
  }

  const DWORD bufferSize         = MAX_PATH;
  wchar_t buffer[bufferSize + 1] = {};

  const auto realSize = ::GetProcessImageFileNameW(process, buffer, bufferSize);

  if (realSize == 0) {
    const auto e = ::GetLastError();
    log::error("GetProcessImageFileNameW() failed, {}", formatSystemMessage(e));
    return badName;
  }

  auto s = QString::fromWCharArray(buffer, realSize);

  const auto lastSlash = s.lastIndexOf("\\");
  if (lastSlash != -1) {
    s = s.mid(lastSlash + 1);
  }

  return s;
}

DWORD getProcessParentID(DWORD pid)
{
  DWORD ppid = 0;

  forEachRunningProcess([&](auto&& entry) {
    if (entry.th32ProcessID == pid) {
      ppid = entry.th32ParentProcessID;
      return false;
    }

    return true;
  });

  return ppid;
}

DWORD getProcessParentID(HANDLE handle)
{
  return getProcessParentID(GetProcessId(handle));
}

}  // namespace env