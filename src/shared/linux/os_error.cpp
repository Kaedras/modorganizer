#include "../os_error.h"

#include <cstring>
#include <format>

namespace MOShared
{

std::string os_error::constructMessage(const std::string& input, int inErrorCode)
{
  int errorCode = inErrorCode != -1 ? inErrorCode : errno;
  return std::format("{} ({} [{}] )", input, strerror(errorCode), errorCode);
}

}  // namespace MOShared
