#include "iconfetcher.h"
#include "shared/util.h"
#include "thread_utils.h"

// use 256 for now
// TODO: find a better solution
static inline constexpr int iconSize = 256;

IconFetcher::IconFetcher() : m_iconSize(iconSize), m_stop(false)
{
  m_quickCache.file      = getPixmapIcon(QFileIconProvider::File);
  m_quickCache.directory = getPixmapIcon(QFileIconProvider::Folder);

  m_thread = MOShared::startSafeThread([&] {
    threadFun();
  });
}