#include <QString>
#include <Windows.h>
#include <utility.h>

using namespace MOBase;

bool isFileLocked(const QString& fileName)
{
  HANDLE file = ::CreateFile(ToWString(fileName).c_str(), GENERIC_READ | GENERIC_WRITE,
                             0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (::GetLastError() == ERROR_SHARING_VIOLATION) {
      return true;
    }
  }
  CloseHandle(file);
  return false;
}
