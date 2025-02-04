#include "compatibility.h"
#include "envfs.h"
#include "env.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

using namespace MOBase;
using namespace std;

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
      close(h);
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

void forEachEntryImpl(void* cx, const QString& path, std::size_t depth,
                      DirStartF* dirStartF,
                      DirEndF* dirEndF, FileF* fileF)
{
  for (const QDirListing::DirEntry& dirEntry : QDirListing(QDir(path).path())) {
    if (dirEntry.isDir()) {
      if (dirStartF && dirEndF) {
        dirStartF(cx, dirEntry.filePath());
        forEachEntryImpl(cx, dirEntry.filePath(), depth + 1, dirStartF,
                         dirEndF,
                         fileF);
        dirEndF(cx, dirEntry.filePath());
      }
    } else {
      fileF(cx, dirEntry.filePath(),
            dirEntry.lastModified(QTimeZone::LocalTime), dirEntry.size());
    }
  }
}

void DirectoryWalker::forEachEntry(const QString& path, void* cx,
                                   DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  forEachEntryImpl(cx, path, 0, dirStartF, dirEndF, fileF);
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
      [](void* pcx, QString path) {
        Context* cx = static_cast<Context*>(pcx);

        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, [[maybe_unused]] QString path) {
        Context* cx = (Context*)pcx;
        cx->current.pop();
      },

      [](void* pcx, QString path, QDateTime ft, qint64 s) {
        Context* cx = (Context*)pcx;

        cx->current.top()->files.push_back(File(path, ft, s));
      });

  return root;
}

File::File(QString n, QDateTime ft, qint64 s)
  : name(n), lcname(n.toLower()), lastModified(ft),
    size(s) {}

Directory::Directory() {}

Directory::Directory(QString n)
  : name(n), lcname(n.toLower()) {}

void getFilesAndDirsWithFindImpl(const std::filesystem::path& path, Directory& d)
{
  for (const QDirListing::DirEntry& dirEntry : QDirListing(QDir(path).path())) {
    if (dirEntry.isDir()) {
      d.dirs.emplace_back(Directory(dirEntry.filePath()));
    }else {
      d.files.emplace_back(File(dirEntry.filePath(), dirEntry.lastModified(QTimeZone::LocalTime),
                                dirEntry.size()));
    }
  }
}

Directory getFilesAndDirsWithFind(const std::wstring& path)
{
  Directory d;
  getFilesAndDirsWithFindImpl(path, d);
  return d;
}

} // namespace env