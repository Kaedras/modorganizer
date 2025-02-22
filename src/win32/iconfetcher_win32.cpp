#include "iconfetcher.h"
#include "shared/util.h"
#include "thread_utils.h"

IconFetcher::IconFetcher() : m_iconSize(GetSystemMetrics(SM_CXSMICON)), m_stop(false)
{
  m_quickCache.file      = getPixmapIcon(QFileIconProvider::File);
  m_quickCache.directory = getPixmapIcon(QFileIconProvider::Folder);

  m_thread = MOShared::startSafeThread([&] {
    threadFun();
  });
}

QVariant IconFetcher::icon(const QString& path) const
{
  if (hasOwnIcon(path)) {
    return fileIcon(path);
  } else {
    const auto dot = path.lastIndexOf(".");

    if (dot == -1) {
      // no extension
      return m_quickCache.file;
    }

    return extensionIcon(QStringView{path}.mid(dot));
  }
}

void IconFetcher::checkCache(Cache& cache)
{
  std::set<QString> queue;

  {
    std::scoped_lock lock(cache.queueMutex);
    queue = std::move(cache.queue);
    cache.queue.clear();
  }

  if (queue.empty()) {
    return;
  }

  std::map<QString, QPixmap> map;
  for (auto&& ext : queue) {
    map.emplace(std::move(ext), getPixmapIcon(QFileInfo(ext)));
  }

  {
    std::scoped_lock lock(cache.mapMutex);
    for (auto&& p : map) {
      cache.map.insert(std::move(p));
    }
  }
}