#include "env.h"
#include "envshortcut.h"
#include "executableslist.h"
#include <QBuffer>
#include <log.h>
#include <utility.h>

using namespace Qt::StringLiterals;

namespace
{

QString iconPath(int resolution)
{
  QString res = QString::number(resolution);
  return QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)
             .first() %
         "/icons/hicolor/"_L1 % res % "x"_L1 % res % "/apps/"_L1;
}

}  // namespace

namespace env
{

using namespace MOBase;

bool Shortcut::add(Locations loc)
{
  // extract icon from the PE file
  if (m_icon.endsWith("exe"_L1, Qt::CaseInsensitive)) {
    QIcon icon     = MOBase::iconForExecutable(m_icon);
    QPixmap pixmap = icon.pixmap(512);
    QImage image   = pixmap.toImage();
    m_icon         = iconPath(image.size().width()) % "mo2-"_L1 % m_name % ".png"_L1;
    // save icon to disk
    if (!image.save(m_icon)) {
      log::warn("error saving icon file {}", m_icon);
    } else {
      log::debug("created icon {}", m_icon);
    }
  }

  log::debug("adding shortcut to {}:\n"
             "  . name: '{}'\n"
             "  . target: '{}'\n"
             "  . arguments: '{}'\n"
             "  . description: '{}'\n"
             "  . icon: '{}'\n"
             "  . working directory: '{}'",
             toString(loc), m_name, m_target, m_arguments, m_description, m_icon,
             m_workingDirectory);

  if (m_target.isEmpty()) {
    log::error("shortcut: target is empty");
    return false;
  }

  QString path;
  switch (loc) {
  case Desktop:
    path = QStandardPaths::standardLocations(QStandardPaths::DesktopLocation).first();
    break;
  case StartMenu:
    path =
        QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation).first();
    break;
  default:
    log::error("shortcut: Location is none");
    return false;
  }

  log::debug("shortcut file will be saved at '{}'", path);

  QString fileContent =
      QStringLiteral("#!/usr/bin/env xdg-open\n\n"
                     "[Desktop Entry]\n"
                     "Name=%1\n"
                     "Exec=%2 %3\n"
                     "Icon=%4\n"
                     "Path=%5\n"
                     "StartupNotify=true\n"
                     "Type=Application\n")
          .arg(m_name, m_target, m_arguments, m_icon, m_workingDirectory);

  QFile file(shortcutPath(loc));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    log::error("could not create shortcut at {}: {}", file.fileName(),
               file.errorString());
    return false;
  }

  QTextStream out(&file);
  out << fileContent;
  return true;
}

}  // namespace env
