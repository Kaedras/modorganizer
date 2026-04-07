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
  // information was found here: https://stackoverflow.com/a/3445031
  // values can be found in the Application Compatibility Toolkit

  // non-exhaustive list of compatibility mode values:
  //    WIN95
  //    WIN98
  //    NT4SP5
  //    WIN2000
  //    WINXPSP2
  //    WINXPSP3
  //    VISTARTM
  //    VISTASP1
  //    VISTASP2
  //    WIN7RTM
  //    WINSRV03SP1
  //    WINSRV08SP1
  //    WIN8RTM

  DWORD dataLength;
  std::wstring applicationFilePath = QApplication::applicationFilePath().toStdWString();
  auto key = LR"(Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers)";

  // get required buffer size
  auto result = RegGetValueW(HKEY_CURRENT_USER, key, applicationFilePath.c_str(),
                             RRF_RT_REG_SZ, nullptr, nullptr, &dataLength);
  if (result != ERROR_SUCCESS) {
    // ERROR_FILE_NOT_FOUND means that no compatibility options are set
    if (result != ERROR_FILE_NOT_FOUND) {
      log::error("Error getting compatibility mode from registry, {}",
                 windowsErrorString(result));
    }
    return false;
  }

  // get data
  std::vector<char> data(dataLength);
  result = RegGetValueW(HKEY_CURRENT_USER, key, applicationFilePath.c_str(),
                        RRF_RT_REG_SZ, nullptr, data.data(), &dataLength);
  if (result != ERROR_SUCCESS) {
    log::error("Error getting compatibility mode from registry, {}",
               windowsErrorString(result));
    return false;
  }

  // all values are on a single line, separated by spaces
  std::wstring str{data.data(), data.data() + dataLength};
  if (str.contains(L" WIN") || str.contains(L" VISTA") || str.contains(L" NT")) {
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
