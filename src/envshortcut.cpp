#include "envshortcut.h"
#include "env.h"
#include "executableslist.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;
using namespace Qt::StringLiterals;

Shortcut::Shortcut() : m_iconIndex(0) {}

Shortcut::Shortcut(const Executable& exe) : Shortcut()
{
  const auto i = *InstanceManager::singleton().currentInstance();

  m_name   = MOBase::sanitizeFileName(exe.title());
  m_target = QFileInfo(qApp->applicationFilePath()).absoluteFilePath();

  m_arguments = QStringLiteral("\"moshortcut://%1:%2\"")
                    .arg(i.isPortable() ? "" : i.displayName())
                    .arg(exe.title());

  m_description = QStringLiteral("Run %1 with ModOrganizer").arg(exe.title());

  if (exe.usesOwnIcon()) {
    m_icon = exe.binaryInfo().absoluteFilePath();
  }

  m_workingDirectory = qApp->applicationDirPath();
}

Shortcut& Shortcut::name(const QString& s)
{
  m_name = MOBase::sanitizeFileName(s);
  return *this;
}

Shortcut& Shortcut::target(const QString& s)
{
  m_target = s;
  return *this;
}

Shortcut& Shortcut::arguments(const QString& s)
{
  m_arguments = s;
  return *this;
}

Shortcut& Shortcut::description(const QString& s)
{
  m_description = s;
  return *this;
}

Shortcut& Shortcut::icon(const QString& s, int index)
{
  m_icon      = s;
  m_iconIndex = index;
  return *this;
}

Shortcut& Shortcut::workingDirectory(const QString& s)
{
  m_workingDirectory = s;
  return *this;
}

bool Shortcut::exists(Locations loc) const
{
  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  return QFileInfo(path).exists();
}

bool Shortcut::toggle(Locations loc)
{
  if (exists(loc)) {
    return remove(loc);
  } else {
    return add(loc);
  }
}

QString Shortcut::shortcutPath(Locations loc) const
{
  const auto dir = shortcutDirectory(loc);
  if (dir.isEmpty()) {
    return {};
  }

  const auto file = shortcutFilename();
  if (file.isEmpty()) {
    return {};
  }

  return dir + QDir::separator() + file;
}

QString Shortcut::shortcutDirectory(Locations loc) const
{
  QString dir;

  try {
    switch (loc) {
    case Desktop:
      dir = MOBase::getDesktopDirectory();
      break;

    case StartMenu:
      dir = MOBase::getStartMenuDirectory();
      break;

    case None:
    default:
      log::error("shortcut: bad location {}", loc);
      break;
    }
  } catch (std::exception&) {
  }

  return QDir::toNativeSeparators(dir);
}

QString Shortcut::shortcutFilename() const
{
  if (m_name.isEmpty()) {
    log::error("shortcut name is empty");
    return {};
  }

  return m_name % u".lnk"_s;
}

QString toString(Shortcut::Locations loc)
{
  switch (loc) {
  case Shortcut::None:
    return u"none"_s;

  case Shortcut::Desktop:
    return u"desktop"_s;

  case Shortcut::StartMenu:
    return u"start menu"_s;

  default:
    return QStringLiteral("? (%1)").arg(static_cast<int>(loc));
  }
}

}  // namespace env
