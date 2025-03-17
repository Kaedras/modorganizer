#include "envshortcut.h"
#include <log.h>
#include <uibase/peextractor.h>
#include <utility.h>

namespace env
{

using namespace MOBase;
using namespace Qt::StringLiterals;

inline QString iconPath(int resolution)
{
  QString res = QString::number(resolution);
  return QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)
             .first() %
         u"/icons/hicolor/"_s % res % u"x"_s % res % u"/apps/"_s;
}

bool Shortcut::add(Locations loc)
{
  // extract icon from PE file
  if (m_icon.endsWith("exe"_L1, Qt::CaseInsensitive)) {
    // open file
    QFile file(m_icon);
    if (file.open(QIODeviceBase::ReadOnly)) {
      // extract data
      QBuffer buff;
      if (buff.open(QIODeviceBase::ReadWrite)) {
        if (PeExtractor::loadIconData(&file, &buff)) {
          QImage image = QImage::fromData(buff.data());
          m_icon = iconPath(image.size().width()) % u"mo2-"_s % m_name % u".png"_s;
          // save icon to disk
          if (!image.save(m_icon)) {
            log::warn("error saving icon file {}", m_icon);
          } else {
            log::debug("created icon {}", m_icon);
          }
        } else {
          log::warn("error in loadIconData");
        }
      } else {
        log::warn("error creating buffer for icon extraction");
      }
    } else {
      log::warn("error opening {} for icon extraction, {}", m_icon, file.errorString());
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
      QString("#!/usr/bin/env xdg-open\n\n"
              "[Desktop Entry]\n"
              "Name=%1\n"
              "Exec=%2 %3\n"
              "Icon=%4\n"
              "Path=%5\n"
              "StartupNotify=true\n"
              "Type=Application\n")
          .arg(m_name, m_target, m_arguments, m_icon, m_workingDirectory);

  QFile file = shortcutPath(loc);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    log::error("could not create shortcut at {}: {}", file.fileName(),
               file.errorString());
    return false;
  }

  QTextStream out(&file);
  out << fileContent;
  return true;
}

bool Shortcut::remove(Locations loc)
{
  // todo: clean up orphaned icon files
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