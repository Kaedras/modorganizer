#include "profile.h"
#include "shared/util.h"

using namespace MOShared;

QString Profile::getPluginsFileName() const
{
  // plugins.txt has to be retrieved case-insensitively
  const QString fileName =
      findFileNameCaseInsensitive(m_Directory, QStringLiteral("plugins.txt"));
  return QDir::cleanPath(m_Directory.absoluteFilePath(fileName));
}
