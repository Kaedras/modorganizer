#include "../os_error.h"

#include <cstring>
#include <format>
#include <sstream>

namespace MOShared
{

std::string os_error::constructMessage(const std::string& input, int inErrorCode)
{
  int errorCode = inErrorCode != -1 ? inErrorCode : GetLastError();
  return std::format("{} ({} [{}] )", input, strerror(errorCode), errorCode);
}

}  // namespace MOShared
