#include "env.h"
#include "instancemanager.h"
#include "settings.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <utility.h>

using namespace MOBase;
using namespace Qt::StringLiterals;

static const QString nexusApiSecretKey = u"ModOrganizer2-Nexusmods-APIKEY"_s;
static const QString steamUsernameKey  = u"steam_username"_s;
static const QString steamPasswordKey  = u"steam_password"_s;

void NexusSettings::dump() const
{
  const QString iniPath = InstanceManager::singleton().globalInstancesRootPath() % u"/"_s %
                          AppConfig::nxmHandlerIni();

  if (!QFileInfo::exists(iniPath)) {
    log::debug("nxm ini not found at {}", iniPath);
    return;
  }

  QSettings s(iniPath, QSettings::IniFormat);
  if (s.status() != QSettings::NoError) {
    log::debug("can't read nxm ini from {}", iniPath);
    return;
  }

  log::debug("nxmhandler settings:");

  // TODO: add nxm handler settings
  // QSettings handler("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\",
  //                   QSettings::NativeFormat);
  // log::debug(" . primary: {}",
  // handler.value("shell/open/command/Default").toString());

  const auto noregister = getOptional<bool>(s, u"General"_s, u"noregister"_s);

  if (noregister) {
    log::debug(" . noregister: {}", *noregister);
  } else {
    log::debug(" . noregister: (not found)");
  }

  ScopedReadArray sra(s, "handlers");

  sra.for_each([&] {
    const auto games      = sra.get<QVariant>(u"games"_s);
    const auto executable = sra.get<QVariant>(u"executable"_s);
    const auto arguments  = sra.get<QVariant>(u"arguments"_s);

    log::debug(" . handler:");
    log::debug("    . games:      {}", games.toString());
    log::debug("    . executable: {}", executable.toString());
    log::debug("    . arguments:  {}", arguments.toString());
  });
}

bool SteamSettings::login(QString& username, QString& password) const
{
  username = get<QString>(m_Settings, u"Settings"_s, steamUsernameKey, "");
  password = getSecret(steamPasswordKey);

  return !username.isEmpty() && !password.isEmpty();
}

void SteamSettings::setLogin(QString username, QString password)
{
  qInfo() << __PRETTY_FUNCTION__;
  if (username.isEmpty()) {
    remove(m_Settings, u"Settings"_s, steamUsernameKey);
    password.clear();
  } else {
    set(m_Settings, u"Settings"_s, steamUsernameKey, username);
  }

  try {
    setSecret(steamPasswordKey, password);
  } catch (const std::runtime_error& e) {
    log::error("Storing or deleting password failed: {}", e.what());
  }
}

bool GlobalSettings::nexusApiKey(QString& apiKey)
{
  try {
    QString tempKey = getSecret(nexusApiSecretKey);
    if (tempKey.isEmpty())
      return false;

    apiKey = tempKey;
    return true;
  } catch (const std::runtime_error& e) {
    log::error("error getting nexus api key: {}", e.what());
    return false;
  }
  return false;
}

bool GlobalSettings::setNexusApiKey(const QString& apiKey)
{
  try {
    setSecret(nexusApiSecretKey, apiKey);
    return true;
  } catch (const std::runtime_error& e) {
    log::error("Storing API key failed: {}", e.what());
    return false;
  }
}

bool GlobalSettings::hasNexusApiKey()
{
  return !getSecret(nexusApiSecretKey).isEmpty();
}
