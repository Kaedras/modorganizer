#include "envfs.h"
#include "env.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

#ifdef __unix__
#include "linux/compatibility.h"
#endif


using namespace MOBase;

namespace env
{


class HandleCloserThread
{
public:
  HandleCloserThread() : m_ready(false) { m_handles.reserve(50000); }

  void shrink() { m_handles.shrink_to_fit(); }

  void add(HANDLE h) { m_handles.push_back(h); }

  void wakeup()
  {
    {
      std::unique_lock lock(m_mutex);
      m_ready = true;
    }

    m_cv.notify_one();
  }

  void run()
  {
    MOShared::SetThisThreadName("HandleCloserThread");

    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] {
      return m_ready;
    });

    closeHandles();
  }

private:
  std::vector<HANDLE> m_handles;
  std::condition_variable m_cv;
  std::mutex m_mutex;
  bool m_ready;

  void closeHandles()
  {
    for (auto& h : m_handles) {
      NtClose(h);
    }

    m_handles.clear();
    m_ready = false;
  }
};

constexpr std::size_t AllocSize = 1024 * 1024;
static ThreadPool<HandleCloserThread> g_handleClosers;

void setHandleCloserThreadCount(std::size_t n)
{
  g_handleClosers.setMax(n);
}

void forEachEntryImpl(void* cx, HandleCloserThread& hc,
                      std::vector<std::unique_ptr<unsigned char[]>>& buffers,
                      POBJECT_ATTRIBUTES poa, std::size_t depth, DirStartF* dirStartF,
                      DirEndF* dirEndF, FileF* fileF)
{
  IO_STATUS_BLOCK iosb;
  UNICODE_STRING ObjectName;
  OBJECT_ATTRIBUTES oa = {sizeof(oa), 0, &ObjectName};
  NTSTATUS status;

  status = NtOpenFile(&oa.RootDirectory, FILE_GENERIC_READ, poa, &iosb,
                      FILE_SHARE_VALID_FLAGS,
                      FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);

  if (status < 0) {
    log::error("failed to open directory '{}': {}", toString(poa),
               formatNtMessage(status));

    return;
  }

  hc.add(oa.RootDirectory);
  unsigned char* buffer;

  if (depth >= buffers.size()) {
    buffers.emplace_back(std::make_unique<unsigned char[]>(AllocSize));
    buffer = buffers.back().get();
  } else {
    buffer = buffers[depth].get();
  }

  union
  {
    PVOID pv;
    PBYTE pb;
    PFILE_DIRECTORY_INFORMATION DirInfo;
  };

  for (;;) {
    status =
        NtQueryDirectoryFile(oa.RootDirectory, NULL, NULL, NULL, &iosb, buffer,
                             AllocSize, FileDirectoryInformation, FALSE, NULL, FALSE);

    if (status == STATUS_NO_MORE_FILES) {
      break;
    } else if (status < 0) {
      log::error("failed to read directory '{}': {}", toString(poa),
                 formatNtMessage(status));

      break;
    }

    ULONG NextEntryOffset = 0;

    pv = buffer;

    auto isDotDir = [](auto* o) {
      if (o->Length == 2 && o->Buffer[0] == '.') {
        return true;
      }

      if (o->Length == 4 && o->Buffer[0] == '.' && o->Buffer[1] == '.') {
        return true;
      }

      return false;
    };

    std::size_t count = 0;

    for (;;) {
      ++count;
      pb += NextEntryOffset;

      ObjectName.Buffer = DirInfo->FileName;
      ObjectName.Length = (USHORT)DirInfo->FileNameLength;

      if (!isDotDir(&ObjectName)) {
        ObjectName.MaximumLength = ObjectName.Length;

        if (DirInfo->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
          if (dirStartF && dirEndF) {
            dirStartF(cx, toStringView(&oa));
            forEachEntryImpl(cx, hc, buffers, &oa, depth + 1, dirStartF, dirEndF,
                             fileF);
            dirEndF(cx, toStringView(&oa));
          }
        } else {
          FILETIME ft;
          ft.dwLowDateTime  = DirInfo->LastWriteTime.LowPart;
          ft.dwHighDateTime = DirInfo->LastWriteTime.HighPart;

          fileF(cx, toStringView(&oa), ft, DirInfo->AllocationSize.QuadPart);
        }
      }

      NextEntryOffset = DirInfo->NextEntryOffset;

      if (NextEntryOffset == 0) {
        break;
      }
    }
  }
}

std::wstring makeNtPath(const std::wstring& path)
{
  constexpr const wchar_t* nt_prefix     = L"\\??\\";
  constexpr const wchar_t* nt_unc_prefix = L"\\??\\UNC\\";
  constexpr const wchar_t* share_prefix  = L"\\\\";

  if (path.starts_with(nt_prefix)) {
    // already an nt path
    return path;
  } else if (path.starts_with(share_prefix)) {
    // network shared need \??\UNC\ as a prefix
    return nt_unc_prefix + path.substr(2);
  } else {
    // prepend the \??\ prefix
    return nt_prefix + path;
  }
}

void DirectoryWalker::forEachEntry(const QString& path, void* cx,
                                   DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  auto& hc = g_handleClosers.request();

  if (!NtOpenFile) {
    LibraryPtr m(::LoadLibraryW(L"ntdll.dll"));
    NtOpenFile = (NtOpenFile_type)::GetProcAddress(m.get(), "NtOpenFile");
    NtQueryDirectoryFile =
        (NtQueryDirectoryFile_type)::GetProcAddress(m.get(), "NtQueryDirectoryFile");
    NtClose = (NtClose_type)::GetProcAddress(m.get(), "NtClose");
  }

  const std::wstring ntpath = makeNtPath(path);

  UNICODE_STRING ObjectName = {};
  ObjectName.Buffer         = const_cast<wchar_t*>(ntpath.c_str());
  ObjectName.Length         = (USHORT)ntpath.size() * sizeof(wchar_t);
  ObjectName.MaximumLength  = ObjectName.Length;

  OBJECT_ATTRIBUTES oa = {};
  oa.Length            = sizeof(oa);
  oa.ObjectName        = &ObjectName;

  forEachEntryImpl(cx, hc, m_buffers, &oa, 0, dirStartF, dirEndF, fileF);
  hc.wakeup();
}

void forEachEntry(const QString& path, void* cx, DirStartF* dirStartF,
                  DirEndF* dirEndF, FileF* fileF)
{
  DirectoryWalker().forEachEntry(path, cx, dirStartF, dirEndF, fileF);
}

Directory getFilesAndDirs(const QString& path)
{
  struct Context
  {
    std::stack<Directory*> current;
  };

  Directory root;

  Context cx;
  cx.current.push(&root);

  env::forEachEntry(
      path, &cx,
      [](void* pcx, const QString& path) {
        Context* cx = (Context*)pcx;

        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, const QString& path) {
        Context* cx = (Context*)pcx;
        cx->current.pop();
      },

      [](void* pcx, const QString& path, QDateTime ft, uint64_t s) {
        Context* cx = (Context*)pcx;

        cx->current.top()->files.push_back(File(path, ft, s));
      });

  return root;
}

File::File(const QString& n, QDateTime ft, uint64_t s)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name)), lastModified(ft),
      size(s)
{}

Directory::Directory() {}

Directory::Directory(const QString& n)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name))
{}

void getFilesAndDirsWithFindImpl(const QString& path, Directory& d)
{
  const QString searchString = path + "/*";

  WIN32_FIND_DATAW findData;

  HANDLE searchHandle =
      ::FindFirstFileExW(searchString.c_str(), FindExInfoBasic, &findData,
                         FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);

  if (searchHandle != INVALID_HANDLE_VALUE) {
    BOOL result = true;

    while (result) {
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if ((wcscmp(findData.cFileName, L".") != 0) &&
            (wcscmp(findData.cFileName, L"..") != 0)) {
          const QString newPath = path + "/" + findData.cFileName;
          d.dirs.push_back(Directory(findData.cFileName));
          getFilesAndDirsWithFindImpl(newPath, d.dirs.back());
        }
      } else {
        const auto size =
            (findData.nFileSizeHigh * (MAXDWORD + 1)) + findData.nFileSizeLow;

        d.files.push_back(File(findData.cFileName, findData.ftLastWriteTime, size));
      }

      result = ::FindNextFileW(searchHandle, &findData);
    }
  }

  ::FindClose(searchHandle);
}

Directory getFilesAndDirsWithFind(const QString& path)
{
  Directory d;
  getFilesAndDirsWithFindImpl(path, d);
  return d;
}

}  // namespace env
