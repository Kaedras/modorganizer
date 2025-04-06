#include "envfs.h"
#include "compatibility.h"
#include "env.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

using namespace MOBase;
using namespace std;

namespace env
{

void setHandleCloserThreadCount(std::size_t) {}

void forEachEntryImpl(void* cx, const QString& path, std::size_t depth,
                      DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  for (const QDirListing::DirEntry& dirEntry : QDirListing(QDir(path).path())) {
    if (dirEntry.isDir()) {
      if (dirStartF && dirEndF) {
        dirStartF(cx, dirEntry.fileName());
        forEachEntryImpl(cx, dirEntry.filePath(), depth + 1, dirStartF, dirEndF, fileF);
        dirEndF(cx, dirEntry.fileName());
      }
    } else {
      fileF(cx, dirEntry.fileName(), dirEntry.lastModified(QTimeZone::LocalTime),
            dirEntry.size());
    }
  }
}

void DirectoryWalker::forEachEntry(const QString& path, void* cx, DirStartF* dirStartF,
                                   DirEndF* dirEndF, FileF* fileF)
{
  forEachEntryImpl(cx, path, 0, dirStartF, dirEndF, fileF);
}

void forEachEntry(const QString& path, void* cx, DirStartF* dirStartF, DirEndF* dirEndF,
                  FileF* fileF)
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
        Context* cx = static_cast<Context*>(pcx);

        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, [[maybe_unused]] const QString& path) {
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
    : name(n), lowerName(n.toLower()), lastModified(ft), size(s)
{}

Directory::Directory() {}

Directory::Directory(const QString& n) : name(n), lowerName(n.toLower()) {}

}  // namespace env