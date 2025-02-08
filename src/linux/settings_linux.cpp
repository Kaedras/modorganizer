#include "env.h"
#include "instancemanager.h"
#include "settings.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <utility.h>

#include "stub.h"

// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Secret.html

void NexusSettings::dump() const
{
  STUB();
}
bool SteamSettings::login(QString& username, QString& password) const
{
  STUB();
  return false;
}
void SteamSettings::setLogin(QString username, QString password)
{
  STUB();
}
bool GlobalSettings::nexusApiKey(QString& apiKey)
{
  STUB();
  return false;
}
bool GlobalSettings::setNexusApiKey(const QString& apiKey)
{
  STUB();
  return false;
}
bool GlobalSettings::hasNexusApiKey()
{
  STUB();
  return false;
}
