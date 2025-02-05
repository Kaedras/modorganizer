#include "envshortcut.h"
#include <log.h>
#include <utility.h>
#include <QtDBus/QtDBus>

namespace env
{

using namespace MOBase;


bool Shortcut::add(Locations loc)
{
  log::debug("adding shortcut to {}:\n"
             "  . name: '{}'\n"
             "  . target: '{}'\n"
             "  . arguments: '{}'\n"
             "  . description: '{}'\n"
             "  . icon: '{}' @ {}\n"
             "  . working directory: '{}'",
             toString(loc), m_name, m_target, m_arguments, m_description, m_icon,
             m_iconIndex, m_workingDirectory);

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
    path = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation).
        first();
    break;
  default:
    log::error("shortcut: Location is none");
    return false;
  }

  log::debug("shortcut file will be saved at '{}'", path);

  QString fileContent =
      QString("#!/usr/bin/env xdg-open\n"
          "[Desktop Entry]\n"
          "Name=%1\n"
          "Exec=\"%2 %3\"\n"
          "Icon=\"%4\"\n"
          "Path=%5\n"
          "StartupNotify=true\n"
          "Type=Application\n"
          ).arg(m_name, m_target, m_arguments, m_icon, m_workingDirectory);

  QFile file(path + "/" + m_name + ".desktop");
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    log::error("could not create shortcut at {}: {}", file.fileName(),
               file.errorString());
    return false;
  }

  QTextStream out(&file);
  out << fileContent;
  file.close();
  return true;

  // TODO: use xdg-desktop portal
  // QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.portal.DynamicLauncher.PrepareInstall");
  // QDBusMessage message = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "org/freedesktop/portal/Desktop", "org.freedesktop.portal.DynamicLauncher", "SupportedLauncherTypes");
}

bool Shortcut::remove(Locations loc)
{
  log::debug("removing shortcut for '{}' from {}", m_name, toString(loc));

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("path to shortcut file is '{}'", path);

  if (!QFile::exists(path)) {
    log::error("can't remove shortcut '{}', file not found", path);
    return false;
  }

  if (!QFile::remove(path)) {
    const auto e = errno;

    log::error("failed to remove shortcut '{}', {}", path, strerror(e));

    return false;
  }

  return true;
}

}  // namespace env