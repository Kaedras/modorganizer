#include "env.h"
#include "envmetrics.h"
#include "executableslist.h"
#include "instancemanager.h"
#include "modelutils.h"
#include "serverinfo.h"
#include "settings.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <expanderwidget.h>
#include <iplugingame.h>
#include <utility.h>

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

void GlobalSettings::updateRegistryKey()
{
  const QString OldOrganization  = "Tannin";
  const QString OldApplication   = "Mod Organizer";
  const QString OldInstanceValue = "CurrentInstance";

  const QString OldRootKey = "Software\\" + OldOrganization;

  if (env::registryValueExists(OldRootKey + "\\" + OldApplication, OldInstanceValue)) {
    QSettings old(OldOrganization, OldApplication);
    setCurrentInstance(old.value(OldInstanceValue).toString());
    old.remove(OldInstanceValue);
  }

  env::deleteRegistryKeyIfEmpty(OldRootKey);
}
