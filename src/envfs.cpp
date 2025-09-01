#include "envfs.h"
#include "env.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

using namespace MOBase;

namespace env
{

void forEachEntryImpl(void* cx, const QString& path, std::size_t depth,
                      DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  QDirIterator it(QDir(path).path());
  while (it.hasNext()) {
    QFileInfo info = it.nextFileInfo();
    if (info.isDir()) {
      if (dirStartF && dirEndF) {
        dirStartF(cx, info.fileName());
        forEachEntryImpl(cx, info.filePath(), depth + 1, dirStartF, dirEndF, fileF);
        dirEndF(cx, info.fileName());
      }
    } else {
      fileF(cx, info.fileName(), info.lastModified(QTimeZone::LocalTime), info.size());
    }
  }
}

QString makeNtPath(const QString& path)
{
  static const QString nt_prefix     = QStringLiteral("\\??\\");
  static const QString nt_unc_prefix = QStringLiteral("\\??\\UNC\\");
  static const QString share_prefix  = QStringLiteral("\\\\");

  if (path.startsWith(nt_prefix)) {
    // already an nt path
    return path;
  } else if (path.startsWith(share_prefix)) {
    // network shared need \??\UNC\ as a prefix
    return nt_unc_prefix + path.sliced(2);
  } else {
    // prepend the \??\ prefix
    return nt_prefix + path;
  }
}

void DirectoryWalker::forEachEntry(const QString& path, void* cx, DirStartF* dirStartF,
                                   DirEndF* dirEndF, FileF* fileF)
{
#ifdef _WIN32
  const QString ntpath = makeNtPath(path);
#else
  const QString& ntpath = path;
#endif

  forEachEntryImpl(cx, ntpath, 0, dirStartF, dirEndF, fileF);
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
      [](void* pcx, QStringView path) {
        Context* cx = (Context*)pcx;

        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, QStringView path) {
        Context* cx = (Context*)pcx;
        cx->current.pop();
      },

      [](void* pcx, QStringView path, QDateTime ft, uint64_t s) {
        Context* cx = (Context*)pcx;

        cx->current.top()->files.push_back(File(path, ft, s));
      });

  return root;
}

File::File(QStringView n, QDateTime ft, uint64_t s)
    : name(n.toString()), lcname(name.toLower()), lastModified(ft), size(s)
{}

Directory::Directory() {}

Directory::Directory(QStringView n) : name(n.toString()), lcname(name.toLower()) {}

}  // namespace env
