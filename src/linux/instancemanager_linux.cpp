#include "instancemanager.h"

#include <log.h>

QString Instance::prefixDirectory() const
{
  return m_prefixDir;
}

QString InstanceManager::portablePath() const
{
  QString appImage = qgetenv("APPIMAGE");
  if (!appImage.isEmpty()) {
    const auto lastSlash = appImage.lastIndexOf('/');
    if (lastSlash == -1) {
      MOBase::log::warn("$APPIMAGE does not contain any slashes: {}", appImage);
      return qApp->applicationDirPath();
    }
    return appImage.first(lastSlash);
  }
  return qApp->applicationDirPath();
}
