#include "iconfetcher.h"
#include "shared/util.h"
#include "thread_utils.h"

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

void IconFetcher::threadFun()
{
  MOShared::SetThisThreadName("IconFetcher");

  while (!m_stop) {
    m_waiter.wait();
    if (m_stop) {
      break;
    }

    checkCache(m_extensionCache);
    checkCache(m_fileCache);
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

QVariant IconFetcher::extensionIcon(const QStringView& ext) const
{
  {
    std::scoped_lock lock(m_extensionCache.mapMutex);
    auto itor = m_extensionCache.map.find(ext);
    if (itor != m_extensionCache.map.end()) {
      return itor->second;
    }
  }

  queue(m_extensionCache, ext.toString());
  return {};
}
