#include "profile.h"

QString Profile::getPluginsFileName() const
{
  return QDir::cleanPath(m_Directory.absoluteFilePath("plugins.txt"));
}
