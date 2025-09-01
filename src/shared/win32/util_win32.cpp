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

#include "env.h"
#include "mainwindow.h"
#include "shared/os_error.h"
#include "shared/util.h"

#include <uibase/log.h>
#include <usvfs/usvfs.h>
#include <usvfs/usvfs_version.h>

using namespace MOBase;

namespace MOShared
{

std::string ToString(const std::wstring& source, bool utf8)
{
  std::string result;
  if (source.length() > 0) {
    UINT codepage = CP_UTF8;
    if (!utf8) {
      codepage = AreFileApisANSI() ? GetACP() : GetOEMCP();
    }
    int sizeRequired = ::WideCharToMultiByte(codepage, 0, &source[0], -1, nullptr, 0,
                                             nullptr, nullptr);
    if (sizeRequired == 0) {
      throw os_error("failed to convert string to multibyte");
    }
    // the size returned by WideCharToMultiByte contains zero termination IF -1 is
    // specified for the length. we don't want that \0 in the string because then the
    // length field would be wrong. Because madness
    result.resize(sizeRequired - 1, '\0');
    ::WideCharToMultiByte(codepage, 0, &source[0], (int)source.size(), &result[0],
                          sizeRequired, nullptr, nullptr);
  }

  return result;
}

std::wstring ToWString(const std::string& source, bool utf8)
{
  std::wstring result;
  if (source.length() > 0) {
    UINT codepage = CP_UTF8;
    if (!utf8) {
      codepage = AreFileApisANSI() ? GetACP() : GetOEMCP();
    }
    int sizeRequired = ::MultiByteToWideChar(
        codepage, 0, source.c_str(), static_cast<int>(source.length()), nullptr, 0);
    if (sizeRequired == 0) {
      throw os_error("failed to convert string to wide character");
    }
    result.resize(sizeRequired, L'\0');
    ::MultiByteToWideChar(codepage, 0, source.c_str(),
                          static_cast<int>(source.length()), &result[0], sizeRequired);
  }

  return result;
}

static std::locale loc("");
static auto locToLowerW = [](wchar_t in) -> wchar_t {
  return std::tolower(in, loc);
};

static auto locToLower = [](char in) -> char {
  return std::tolower(in, loc);
};

std::string& ToLowerInPlace(std::string& text)
{
  CharLowerBuffA(const_cast<CHAR*>(text.c_str()), static_cast<DWORD>(text.size()));
  return text;
}

std::string ToLowerCopy(const std::string& text)
{
  std::string result(text);
  CharLowerBuffA(const_cast<CHAR*>(result.c_str()), static_cast<DWORD>(result.size()));
  return result;
}

std::wstring& ToLowerInPlace(std::wstring& text)
{
  CharLowerBuffW(const_cast<WCHAR*>(text.c_str()), static_cast<DWORD>(text.size()));
  return text;
}

std::wstring ToLowerCopy(const std::wstring& text)
{
  std::wstring result(text);
  CharLowerBuffW(const_cast<WCHAR*>(result.c_str()), static_cast<DWORD>(result.size()));
  return result;
}

VS_FIXEDFILEINFO GetFileVersion(const std::wstring& fileName)
{
  DWORD handle = 0UL;
  DWORD size   = ::GetFileVersionInfoSizeW(fileName.c_str(), &handle);
  if (size == 0) {
    throw os_error("failed to determine file version info size");
  }

  boost::scoped_array<char> buffer(new char[size]);
  try {
    handle = 0UL;
    if (!::GetFileVersionInfoW(fileName.c_str(), handle, size, buffer.get())) {
      throw os_error("failed to determine file version info");
    }

    void* versionInfoPtr   = nullptr;
    UINT versionInfoLength = 0;
    if (!::VerQueryValue(buffer.get(), L"\\", &versionInfoPtr, &versionInfoLength)) {
      throw os_error("failed to determine file version");
    }

    VS_FIXEDFILEINFO result = *(VS_FIXEDFILEINFO*)versionInfoPtr;
    return result;
  } catch (...) {
    throw;
  }
}

std::wstring GetFileVersionString(const std::wstring& fileName)
{
  DWORD handle = 0UL;
  DWORD size   = ::GetFileVersionInfoSizeW(fileName.c_str(), &handle);
  if (size == 0) {
    throw os_error("failed to determine file version info size");
  }

  boost::scoped_array<char> buffer(new char[size]);
  try {
    handle = 0UL;
    if (!::GetFileVersionInfoW(fileName.c_str(), handle, size, buffer.get())) {
      throw os_error("failed to determine file version info");
    }

    LPVOID strBuffer = nullptr;
    UINT strLength   = 0;
    if (!::VerQueryValue(buffer.get(), L"\\StringFileInfo\\040904B0\\ProductVersion",
                         &strBuffer, &strLength)) {
      throw os_error("failed to determine file version");
    }

    return std::wstring((LPCTSTR)strBuffer);
  } catch (...) {
    throw;
  }
}

Version createVersionInfo()
{
  VS_FIXEDFILEINFO version = GetFileVersion(env::thisProcessPath().toStdWString());

  std::optional<Version::ReleaseType> releaseType;

  if (version.dwFileFlags & VS_FF_PRERELEASE) {
    // Pre-release builds need annotating
    QString versionString = QString::fromStdWString(
        GetFileVersionString(env::thisProcessPath().toStdWString()));

    // The pre-release flag can be set without the string specifying what type of
    // pre-release
    bool noLetters = true;
    for (QChar character : versionString) {
      if (character.isLetter()) {
        noLetters = false;
        break;
      }
    }

    if (!noLetters) {
      // trust the string to make sense
      return Version::parse(versionString, Version::ParseMode::MO2);
    }

    if (noLetters) {
      // default to development when release type is unspecified
      releaseType = Version::Development;
    } else {
    }
  }

  const int major    = version.dwFileVersionMS >> 16,
            minor    = version.dwFileVersionMS & 0xFFFF,
            patch    = version.dwFileVersionLS >> 16,
            subpatch = version.dwFileVersionLS & 0xFFFF;

  std::vector<std::variant<int, Version::ReleaseType>> prereleases;
  if (releaseType) {
    prereleases.push_back(*releaseType);
  }

  return Version(major, minor, patch, subpatch, std::move(prereleases));
}

QString getUsvfsDLLVersion()
{
  QString s = usvfsVersionString();
  if (s.isEmpty()) {
    s = "?";
  }
  return s;
}

QString getUsvfsVersionString()
{
  const QString dll    = getUsvfsDLLVersion();
  const QString header = USVFS_VERSION_STRING;

  QString usvfsVersion;

  if (dll == header) {
    return dll;
  } else {
    return "dll is " + dll + ", compiled against " + header;
  }
}

void SetThisThreadName(const QString& s)
{
  using SetThreadDescriptionType = HRESULT(HANDLE hThread, PCWSTR lpThreadDescription);

  static SetThreadDescriptionType* SetThreadDescription = [] {
    SetThreadDescriptionType* p = nullptr;

    env::LibraryPtr kernel32(LoadLibraryW(L"kernel32.dll"));
    if (!kernel32) {
      return p;
    }

    p = reinterpret_cast<SetThreadDescriptionType*>(
        GetProcAddress(kernel32.get(), "SetThreadDescription"));

    return p;
  }();

  if (SetThreadDescription) {
    SetThreadDescription(GetCurrentThread(), s.toStdWString().c_str());
  }
}

}  // namespace MOShared
