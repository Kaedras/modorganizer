#include "env.h"
#include "settings.h"
#include "settingsutilities.h"
#include <QProcess>

using namespace MOBase;
using namespace Qt::StringLiterals;

QString getNxmHandler()
{
  // requires xdg-utils to be installed
  QProcess p;
  // this command returns the application used for handling nxm:// urls
  p.startCommand(u"xdg-mime query default x-scheme-handler/nxm"_s);
  if (p.waitForFinished()) {
    if (p.exitCode() == 0) {
      QString out = p.readAllStandardOutput();
      out         = out.trimmed();
      if (out.isEmpty() || out.endsWith(".desktop"_L1)) {
        return out;
      }
      return "(unexpected result: "_L1 % out % ')';
    }

    return "(error "_L1 % QString::number(p.exitCode()) % ')';
  }

  return "(error: "_L1 % p.errorString() % ')';
}

void GlobalSettings::updateRegistryKey()
{
  // no-op
}

QString GameSettings::prefix() const
{
  return get<QString>(m_Settings, u"General"_s, u"prefix_directory"_s, {});
}

void GameSettings::setPrefix(const QString& prefix)
{
  if (prefix.isEmpty()) {
    remove(m_Settings, u"General"_s, u"prefix_directory"_s);
  } else {
    set(m_Settings, u"General"_s, u"prefix_directory"_s, prefix);
  }
}
