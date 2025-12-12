#include "env.h"
#include "instancemanager.h"
#include "settings.h"
#include "settingsutilities.h"
#include "shared/appconfig.h"
#include <utility.h>

using namespace MOBase;
using namespace Qt::StringLiterals;

void NexusSettings::dump() const
{
  const QString iniPath = InstanceManager::singleton().globalInstancesRootPath() %
                          "/"_L1 % AppConfig::nxmHandlerIni();

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

  // requires xdg-utils to be installed
  QProcess p;
  // this command returns the application used for handling nxm:// urls
  p.startCommand(u"xdg-mime query default x-scheme-handler/nxm"_s);
  if (p.waitForFinished()) {
    if (p.exitCode() == 0) {
      QString out = p.readAllStandardOutput();
      out         = out.trimmed();
      if (!out.endsWith(u".desktop"_s)) {
        log::warn("unexpected result when retrieving nxmhandler settings: {}", out);
      } else {
        log::debug(" . primary: {}", out);
      }
    } else {
      log::warn("error retrieving nxmhandler settings: {}", p.exitCode());
    }
  }

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

void GlobalSettings::updateRegistryKey()
{
  // no-op
}

QString GameSettings::prefix() const
{
  return get<QString>(m_Settings, u"General"_s, u"prefix_directory"_s, "");
}

void GameSettings::setPrefix(const QString& prefix)
{
  if (prefix.isEmpty()) {
    remove(m_Settings, u"General"_s, u"prefix_directory"_s);
  } else {
    set(m_Settings, u"General"_s, u"prefix_directory"_s, prefix);
  }
}
