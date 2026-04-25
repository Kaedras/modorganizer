#include "filetreeitem.h"
#include "filetreemodel.h"
#include "modinfo.h"
#include "modinfodialogfwd.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

using namespace MOBase;
using namespace MOShared;
namespace fs = std::filesystem;

const QString& cachedFileType(const QString& file, bool isOnFilesystem);
const QString& directoryFileType();

std::optional<uint64_t> FileTreeItem::fileSize() const
{
  if (m_fileSize.empty() && !m_isDirectory) {
    std::error_code ec;
    const auto size =
        fs::file_size(QFileInfo(m_realPath).filesystemAbsoluteFilePath(), ec);

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
      const QFileInfo fi(m_realPath);
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

  const auto& t = cachedFileType(m_realPath, !isFromArchive());
  if (t.isEmpty()) {
    m_fileType.fail();
  } else {
    m_fileType.set(t);
  }
}
