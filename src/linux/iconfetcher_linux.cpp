#include "iconfetcher.h"
#include "thread_utils.h"

// TODO: find a better solution or make this a setting
static inline constexpr int iconSize = 16;

IconFetcher::IconFetcher() : m_iconSize(iconSize), m_stop(false)
{
  m_quickCache.file      = getPixmapIcon(QFileIconProvider::File);
  m_quickCache.directory = getPixmapIcon(QFileIconProvider::Folder);

  m_thread = MOShared::startSafeThread([&] {
    threadFun();
  });
}

template<>
QPixmap IconFetcher::getPixmapIcon(const QString& t) const
{
  static QMimeDatabase db;
  auto type = db.mimeTypeForName(t);

  return QIcon::fromTheme(type.iconName()).pixmap({iconSize, iconSize});
}

QVariant IconFetcher::icon(const QString& path) const
{
  if (hasOwnIcon(path)) {
    return fileIcon(path);
  } else {
    static QMimeDatabase db;
    auto mimeTypeName = db.mimeTypeForFile(path).name();

    return extensionIcon(mimeTypeName);
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
  for (auto&& type : queue) {
    map.emplace(std::move(type), getPixmapIcon(type));
  }

  {
    std::scoped_lock lock(cache.mapMutex);
    for (auto&& p : map) {
      cache.map.insert(std::move(p));
    }
  }
}
