#include "../envos.h"
#include "env.h"
#include "envmodule.h"
#include "envprocess.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

bool OsInfo::getCompatibilityMode() const
{
  using RtlGetNtVersionNumbersType = void(NTAPI)(DWORD*, DWORD*, DWORD*);

  auto* RtlGetNtVersionNumbers =
      reinterpret_cast<RtlGetNtVersionNumbersType*> GetProcAddress(
          GetModuleHandleA("ntdll.dll"), "RtlGetNtVersionNumbers");

  if (!RtlGetNtVersionNumbers) {
    log::error("RtlGetNtVersionNumbers() not found in ntdll.dll");
    return false;
  }

  OSVERSIONINFO osInfo       = {0};
  osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  GetVersionEx(&osInfo);

  DWORD dwMajorVersion;
  DWORD dwMinorVersion;
  DWORD dwBuildNumber;

  RtlGetNtVersionNumbers(&dwMajorVersion, &dwMinorVersion, &dwBuildNumber);

  dwBuildNumber &= 0x0000FFFF;

  if (osInfo.dwBuildNumber != dwBuildNumber) {
    return true;
  }
  return false;
}

std::optional<bool> OsInfo::getElevated() const
{
  HandlePtr token;

  {
    HANDLE rawToken = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
      const auto e = GetLastError();

      log::error("while trying to check if process is elevated, "
                 "OpenProcessToken() failed: {}",
                 formatSystemMessage(e));

      return {};
    }

    token.reset(rawToken);
  }

  TOKEN_ELEVATION e = {};
  DWORD size        = sizeof(TOKEN_ELEVATION);

  if (!GetTokenInformation(token.get(), TokenElevation, &e, sizeof(e), &size)) {
    const auto e = GetLastError();

    log::error("while trying to check if process is elevated, "
               "GetTokenInformation() failed: {}",
               formatSystemMessage(e));

    return {};
  }

  return (e.TokenIsElevated != 0);
}

}  // namespace env
