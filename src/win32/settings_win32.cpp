#include "env.h"
#include "instancemanager.h"
#include "settings.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <utility.h>

#include <Windows.h>

using namespace MOBase;
using namespace MOShared;

void NexusSettings::dump() const
{
  const auto iniPath = InstanceManager::singleton().globalInstancesRootPath() + "/" +
                       AppConfig::nxmHandlerIni();

  if (!QFileInfo(iniPath).exists()) {
    log::debug("nxm ini not found at {}", iniPath);
    return;
  }

  QSettings s(iniPath, QSettings::IniFormat);
  if (const auto st = s.status(); st != QSettings::NoError) {
    log::debug("can't read nxm ini from {}", iniPath);
    return;
  }

  log::debug("nxmhandler settings:");

  QSettings handler("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\",
                    QSettings::NativeFormat);
  log::debug(" . primary: {}", handler.value("shell/open/command/Default").toString());

  const auto noregister = getOptional<bool>(s, "General", "noregister");

  if (noregister) {
    log::debug(" . noregister: {}", *noregister);
  } else {
    log::debug(" . noregister: (not found)");
  }

  ScopedReadArray sra(s, "handlers");

  sra.for_each([&] {
    const auto games      = sra.get<QVariant>("games");
    const auto executable = sra.get<QVariant>("executable");
    const auto arguments  = sra.get<QVariant>("arguments");

    log::debug(" . handler:");
    log::debug("    . games:      {}", games.toString());
    log::debug("    . executable: {}", executable.toString());
    log::debug("    . arguments:  {}", arguments.toString());
  });
}

bool SteamSettings::login(QString& username, QString& password) const
{
  username = get<QString>(m_Settings, "Settings", "steam_username", "");
  password = getWindowsCredential("steam_password");

  return !username.isEmpty() && !password.isEmpty();
}

void SteamSettings::setLogin(QString username, QString password)
{
  if (username == "") {
    remove(m_Settings, "Settings", "steam_username");
    password = "";
  } else {
    set(m_Settings, "Settings", "steam_username", username);
  }

  if (!setWindowsCredential("steam_password", password)) {
    const auto e = GetLastError();
    log::error("Storing or deleting password failed: {}", formatSystemMessage(e));
  }
}

bool GlobalSettings::nexusApiKey(QString& apiKey)
{
  QString tempKey = getWindowsCredential("APIKEY");
  if (tempKey.isEmpty())
    return false;

  apiKey = tempKey;
  return true;
}

bool GlobalSettings::setNexusApiKey(const QString& apiKey)
{
  if (!setWindowsCredential("APIKEY", apiKey)) {
    const auto e = GetLastError();
    log::error("Storing API key failed: {}", formatSystemMessage(e));
    return false;
  }

  return true;
}

bool GlobalSettings::hasNexusApiKey()
{
  return !getWindowsCredential("APIKEY").isEmpty();
}