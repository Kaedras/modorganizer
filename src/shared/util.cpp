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

#include "util.h"
#include "../env.h"
#include "../mainwindow.h"
#include "os_error.h"
#include <uibase/log.h>

using namespace MOBase;

namespace MOShared
{

bool FileExists(const std::string& filename)
{
  return QFileInfo::exists(QString::fromStdString(filename));
}

bool FileExists(const std::wstring& filename)
{
  return QFileInfo::exists(QString::fromStdWString(filename));
}

bool FileExists(const std::wstring& searchPath, const std::wstring& filename)
{
  std::wstringstream stream;
  stream << searchPath << "/" << filename;
  return FileExists(stream.str());
}

QString& ToLowerInPlace(QString& text)
{
  for (auto& c : text) {
    c = c.toLower();
  }
  return text;
}

QString ToLowerCopy(QStringView text)
{
  QString result = text.toString();
  ToLowerInPlace(result);
  return result;
}

static std::locale loc("");
bool CaseInsenstiveComparePred(wchar_t lhs, wchar_t rhs)
{
  return std::tolower(lhs, loc) == std::tolower(rhs, loc);
}

bool CaseInsensitiveEqual(const std::wstring& lhs, const std::wstring& rhs)
{
  return (lhs.length() == rhs.length()) &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                    [](wchar_t lhs, wchar_t rhs) -> bool {
                      return std::tolower(lhs, loc) == std::tolower(rhs, loc);
                    });
}

bool CaseInsensitiveEqual(const QString& lhs, const QString& rhs)
{
  return lhs.compare(rhs, Qt::CaseInsensitive) == 0;
}

char shortcutChar(const QAction* a)
{
  const auto text = a->text();
  char shortcut   = 0;

  for (int i = 0; i < text.size(); ++i) {
    const auto c = text[i];
    if (c == '&') {
      if (i >= (text.size() - 1)) {
        log::error("ampersand at the end");
        return 0;
      }

      return text[i + 1].toLatin1();
    }
  }

  log::error("action {} has no shortcut", text);
  return 0;
}

void checkDuplicateShortcuts(const QMenu& m)
{
  const auto actions = m.actions();

  for (int i = 0; i < actions.size(); ++i) {
    const auto* action1 = actions[i];
    if (action1->isSeparator()) {
      continue;
    }

    const char shortcut1 = shortcutChar(action1);
    if (shortcut1 == 0) {
      continue;
    }

    for (int j = i + 1; j < actions.size(); ++j) {
      const auto* action2 = actions[j];
      if (action2->isSeparator()) {
        continue;
      }

      const char shortcut2 = shortcutChar(action2);

      if (shortcut1 == shortcut2) {
        log::error("duplicate shortcut {} for {} and {}", shortcut1, action1->text(),
                   action2->text());

        break;
      }
    }
  }
}

}  // namespace MOShared

static bool g_exiting  = false;
static bool g_canClose = false;

MainWindow* findMainWindow()
{
  for (auto* tl : qApp->topLevelWidgets()) {
    if (auto* mw = dynamic_cast<MainWindow*>(tl)) {
      return mw;
    }
  }

  return nullptr;
}

bool ExitModOrganizer(ExitFlags e)
{
  if (g_exiting) {
    return true;
  }

  g_exiting = true;
  Guard g([&] {
    g_exiting = false;
  });

  if (!e.testFlag(Exit::Force)) {
    if (auto* mw = findMainWindow()) {
      if (!mw->canExit()) {
        return false;
      }
    }
  }

  g_canClose = true;

  const int code = (e.testFlag(Exit::Restart) ? RestartExitCode : 0);
  qApp->exit(code);

  return true;
}

bool ModOrganizerCanCloseNow()
{
  return g_canClose;
}

bool ModOrganizerExiting()
{
  return g_exiting;
}

void ResetExitFlag()
{
  g_exiting = false;
}

bool isNxmLink(const QString& link)
{
  return link.startsWith("nxm://", Qt::CaseInsensitive);
}
