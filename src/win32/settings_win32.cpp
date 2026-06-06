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

QString getNxmHandler()
{
  QSettings handler("HKEY_CURRENT_USER\\Software\\Classes\\nxm\\",
                    QSettings::NativeFormat);
  return handler.value("shell/open/command/Default").toString();
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
