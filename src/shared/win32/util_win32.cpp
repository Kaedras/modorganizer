#include "util.h"
#include "../env.h"
#include "../mainwindow.h"
#include "windows_error.h"
#include <uibase/log.h>
#include <usvfs/usvfs.h>
#include <usvfs/usvfs_version.h>

using namespace MOBase;

namespace MOShared
{
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
  VS_FIXEDFILEINFO version = GetFileVersion(env::thisProcessPath().native());

  std::optional<Version::ReleaseType> releaseType;

  if (version.dwFileFlags & VS_FF_PRERELEASE) {
    // Pre-release builds need annotating
    QString versionString =
        QString::fromStdWString(GetFileVersionString(env::thisProcessPath().native()));

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