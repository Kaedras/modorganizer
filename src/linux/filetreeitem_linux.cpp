#include "filetreeitem.h"
#include "filetreemodel.h"
#include "modinfo.h"
#include "shared/util.h"
#include <log.h>

using namespace MOBase;
using namespace MOShared;
namespace fs = std::filesystem;

namespace
{

/**
 * @brief Resolve a given path case-insensitively
 * @param path Path to resolve
 * @return Resolved path, or original path on error
 */
QString resolvePath(const QString& path)
{
  // don't do anything if path exists
  if (QFileInfo::exists(path)) {
    return path;
  }

  QStringList segments = path.split('/', Qt::SkipEmptyParts);

  // create iterator to last element
  auto it = segments.end() - 1;

  // store parent directory
  QString dirStr = path.chopped(it->size() + 1);
  --it;

  // move upwards until an existing directory has been found
  while (!QFileInfo::exists(dirStr)) {
    // remove directory name + '/'
    dirStr = dirStr.chopped(it->size() + 1);
    // decrement path segment index
    --it;
  }

  QDir dir(dirStr);

  ++it;

  // iterate over path segments
  while (it != segments.end()) {
    QStringList entries = dir.entryList({*it}, QDir::Files | QDir::Dirs);
    if (entries.isEmpty()) {
      log::error("error resolving path: entry list for '{}' is empty, filter: '{}'",
                 dir.path(), *it);
      return path;
    }

    // return the file path on last segment
    if (it == segments.end() - 1) {
      return dir.filePath(entries.first());
    }

    // move into the next directory
    if (!dir.cd(entries.first())) {
      log::error("error resolving path, dir({}).cd({}) failed", dir.path(),
                 entries.first());
    }
    ++it;
  }

  log::error("error resolving path {}", path);

  return path;
}
}  // namespace

const QString& cachedFileType(const QString& file, bool isOnFilesystem);
const QString& directoryFileType();

std::optional<uint64_t> FileTreeItem::fileSize() const
{
  if (m_fileSize.empty() && !m_isDirectory) {
    std::error_code ec;
    const auto size = fs::file_size(
        QFileInfo(resolvePath(m_realPath)).filesystemAbsoluteFilePath(), ec);

    if (ec) {
      log::error("can't get file size for '{}', {}", m_realPath, ec.message());
      m_fileSize.fail();
    } else {
      m_fileSize.set(size);
    }
  }

  return m_fileSize.value;
}

std::optional<QDateTime> FileTreeItem::lastModified() const
{
  if (m_lastModified.empty()) {
    if (m_realPath.isEmpty()) {
      // this is a virtual directory
      m_lastModified.set({});
    } else if (isFromArchive()) {
      // can't get last modified date for files in archives
      m_lastModified.set({});
    } else {
      // looks like a regular file on the filesystem
      const QFileInfo fi(resolvePath(m_realPath));
      const auto d = fi.lastModified();

      if (!d.isValid()) {
        log::error("can't get last modified date for '{}'", m_realPath);
        m_lastModified.fail();
      } else {
        m_lastModified.set(d);
      }
    }
  }

  return m_lastModified.value;
}

void FileTreeItem::getFileType() const
{
  if (isDirectory()) {
    m_fileType.set(directoryFileType());
    return;
  }

  const auto& t = cachedFileType(resolvePath(m_realPath), !isFromArchive());
  if (t.isEmpty()) {
    m_fileType.fail();
  } else {
    m_fileType.set(t);
  }
}
