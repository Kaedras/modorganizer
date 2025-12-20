#include "instancemanager.h"

#include <QCoreApplication>
#include <QString>

QString InstanceManager::portablePath() const
{
  return qApp->applicationDirPath();
}
