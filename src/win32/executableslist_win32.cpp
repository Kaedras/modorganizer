#include "executableslist.h"

#include "iplugingame.h"
#include "settings.h"
#include "utility.h"
#include <log.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

Executable::Executable(const MOBase::ExecutableInfo& info, Flags flags)
    : m_title(info.title()), m_binaryInfo(info.binary()),
      m_arguments(info.arguments().join(" ")), m_steamAppID(info.steamAppID()),
      m_workingDirectory(info.workingDirectory().path()), m_flags(flags)
{}

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
               "    flags: {} ({})",
               e.title(), e.binaryInfo().filePath(), e.arguments(), e.steamAppID(),
               e.workingDirectory(), flags.join("|"), e.flags());
  }
}
