#ifndef NATIVESTRING_H
#define NATIVESTRING_H

#include <QString>
#include <string>

#ifdef __unix__
using nativeString  = std::string;
using nativeCString = const char*;
#define NATIVE_STRING(s) s
#define NATIVE_CERR std::cerr

inline std::string ToNativeString(const QString& string)
{
  return string.toStdString();
}

#else
using nativeString  = std::wstring;
using nativeCString = const wchar_t*;
#define NATIVE_STRING(s) L##s
#define NATIVE_CERR std::wcerr

inline std::wstring ToNativeString(const QString& string)
{
  return string.toStdWString();
}
#endif

#endif  // NATIVESTRING_H
