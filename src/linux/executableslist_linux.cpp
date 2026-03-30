#include "executableslist.h"

#include "settings.h"
#include <log.h>

#include <QFileInfo>

using namespace MOBase;

Executable::Executable(const MOBase::ExecutableInfo& info, Flags flags)
    : m_title(info.title()), m_binaryInfo(info.binary()),
      m_arguments(info.arguments().join(" ")), m_steamAppID(info.steamAppID()),
      m_workingDirectory(info.workingDirectory().path()), m_flags(flags),
      m_prefixDirectory(info.prefixDirectory().path()),
      m_enableSteamAPI(info.enableSteamAPI()),
      m_enableSteamOverlay(info.enableSteamOverlay())
{}

const QString& Executable::prefixDirectory() const
{
  return m_prefixDirectory;
}

Executable& Executable::prefixDirectory(const QString& s)
{
  m_prefixDirectory = s;
  return *this;
}

Executable& Executable::enableSteamAPI(bool b)
{
  m_enableSteamAPI = b;
  return *this;
}

Executable& Executable::enableSteamOverlay(bool b)
{
  m_enableSteamOverlay = b;
  return *this;
}

bool Executable::enableSteamAPI() const
{
  return m_enableSteamAPI;
}

bool Executable::enableSteamOverlay() const
{
  return m_enableSteamOverlay;
}

void ExecutablesList::dump() const
{
  for (const auto& e : m_Executables) {
    QStringList flags;

    if (e.flags() & Executable::ShowInToolbar) {
      flags.push_back("toolbar");
    }

    if (e.flags() & Executable::UseApplicationIcon) {
      flags.push_back("icon");
    }

    if (e.flags() & Executable::Hide) {
      flags.push_back("hide");
    }

    if (e.flags() & Executable::MinimizeToSystemTray) {
      flags.push_back("minimizeToSystemTray");
    }

    log::debug(" . executable '{}'\n"
               "    binary: {}\n"
               "    arguments: {}\n"
               "    steam ID: {}\n"
               "    directory: {}\n"
               "    prefix: {}\n"
               "    steam api: {}\n"
               "    steam overlay: {}\n"
               "    flags: {} ({})",
               e.title(), e.binaryInfo().filePath(), e.arguments(), e.steamAppID(),
               e.workingDirectory(), e.prefixDirectory(), e.enableSteamAPI(),
               e.enableSteamOverlay(), flags.join("|"), e.flags());
  }
}
