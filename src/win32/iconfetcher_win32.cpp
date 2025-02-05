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