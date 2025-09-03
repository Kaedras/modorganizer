#include "iconfetcher.h"
#include "shared/util.h"
#include "thread_utils.h"

namespace
{
int getIconSize()
{
#ifdef __unix__
  // TODO: find a better solution or make this a setting
  return 16;
#else
  return GetSystemMetrics(SM_CXSMICON);
#endif
}
}  // namespace

void IconFetcher::Waiter::wait()
{
  std::unique_lock lock(m_wakeUpMutex);
  m_wakeUp.wait(lock, [&] {
    return m_queueAvailable;
  });
  m_queueAvailable = false;
}

void IconFetcher::Waiter::wakeUp()
{
  {
    std::scoped_lock lock(m_wakeUpMutex);
    m_queueAvailable = true;
  }

  m_wakeUp.notify_one();
}

IconFetcher::IconFetcher() : m_iconSize(getIconSize()), m_stop(false)
{
  m_quickCache.file      = getPixmapIcon(QFileIconProvider::File);
  m_quickCache.directory = getPixmapIcon(QFileIconProvider::Folder);

  m_thread = MOShared::startSafeThread([&] {
    threadFun();
  });
}

IconFetcher::~IconFetcher()
{
  stop();
  m_thread.join();
}

void IconFetcher::stop()
{
  m_stop = true;
  m_waiter.wakeUp();
}

QVariant IconFetcher::icon(const QString& path) const
{
  if (hasOwnIcon(path)) {
    return fileIcon(path);
  }

  QMimeDatabase db;
  QString mimeTypeName = db.mimeTypeForFile(path).name();

  return mimeTypeIcon(mimeTypeName);
}

QPixmap IconFetcher::genericFileIcon() const
{
  return m_quickCache.file;
}

QPixmap IconFetcher::genericDirectoryIcon() const
{
  return m_quickCache.directory;
}

bool IconFetcher::hasOwnIcon(const QString& path) const
{
  static const QString exe = ".exe";
  static const QString lnk = ".lnk";
  static const QString ico = ".ico";

  return path.endsWith(exe, Qt::CaseInsensitive) ||
         path.endsWith(lnk, Qt::CaseInsensitive) ||
         path.endsWith(ico, Qt::CaseInsensitive);
}

QPixmap IconFetcher::getPixmapIcon(QFileIconProvider::IconType t) const
{
  return m_provider.icon(t).pixmap({m_iconSize, m_iconSize});
}

QPixmap IconFetcher::getPixmapIcon(const QString& t) const
{
  QMimeDatabase db;
  QMimeType type = db.mimeTypeForName(t);

  return QIcon::fromTheme(type.iconName()).pixmap({m_iconSize, m_iconSize});
}

void IconFetcher::threadFun()
{
  MOShared::SetThisThreadName("IconFetcher");

  while (!m_stop) {
    m_waiter.wait();
    if (m_stop) {
      break;
    }

    checkCache(m_mimeTypeCache);
    checkCache(m_fileCache);
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

void IconFetcher::queue(Cache& cache, QString path) const
{
  {
    std::scoped_lock lock(cache.queueMutex);
    cache.queue.insert(std::move(path));
  }

  m_waiter.wakeUp();
}

QVariant IconFetcher::fileIcon(const QString& path) const
{
  {
    std::scoped_lock lock(m_fileCache.mapMutex);
    auto itor = m_fileCache.map.find(path);
    if (itor != m_fileCache.map.end()) {
      return itor->second;
    }
  }

  queue(m_fileCache, path);
  return {};
}

QVariant IconFetcher::mimeTypeIcon(const QStringView& type) const
{
  {
    std::scoped_lock lock(m_mimeTypeCache.mapMutex);
    auto itor = m_mimeTypeCache.map.find(type);
    if (itor != m_mimeTypeCache.map.end()) {
      return itor->second;
    }
  }

  queue(m_mimeTypeCache, type.toString());
  return {};
}
