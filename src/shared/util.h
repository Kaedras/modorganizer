/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef UTIL_H
#define UTIL_H

#ifdef __unix__
inline QString NativeToQString(const std::string& str)
{
  return QString::fromStdString(str);
}
#else
#include "win32/util_win32.h"
inline QString NativeToQString(const std::wstring& str)
{
  return QString::fromStdWString(str);
}
#endif

#include <filesystem>
#include <string>

#include <uibase/log.h>
#include <uibase/versioning.h>

class Executable;

namespace MOShared
{

/// Test if a file (or directory) by the specified name exists
bool FileExists(const std::string& filename);
bool FileExists(const std::wstring& filename);
bool FileExists(const QString& filename);

bool FileExists(const QString& searchPath, const QString& filename);

MOBase::Version createVersionInfo();
QString getUsvfsVersionString();

void SetThisThreadName(const QString& s);
void checkDuplicateShortcuts(const QMenu& m);

}  // namespace MOShared

enum class Exit
{
  None    = 0x00,
  Normal  = 0x01,
  Restart = 0x02,
  Force   = 0x04
};

const int RestartExitCode  = INT_MAX;
const int ReselectExitCode = INT_MAX - 1;

using ExitFlags = QFlags<Exit>;
Q_DECLARE_OPERATORS_FOR_FLAGS(ExitFlags);

bool ExitModOrganizer(ExitFlags e = Exit::Normal);
bool ModOrganizerExiting();
bool ModOrganizerCanCloseNow();
void ResetExitFlag();

bool isNxmLink(const QString& link);

#endif  // UTIL_H
