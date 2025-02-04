#ifndef UTIL_WIN32_H
#define UTIL_WIN32_H

namespace MOShared
{

inline FILETIME ToFILETIME(std::filesystem::file_time_type t)
{
  FILETIME ft;
  static_assert(sizeof(t) == sizeof(ft));

  std::memcpy(&ft, &t, sizeof(FILETIME));
  return ft;
}

}  // namespace MOShared

#endif //UTIL_WIN32_H
