#include <QString>
#include "../shared/os_error.h"

bool isFileLocked(const QString& fileName) noexcept(false) {
  HANDLE file =
      ::CreateFile(fileName.toStdWString().c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    if (::GetLastError() == ERROR_SHARING_VIOLATION) {
      // file is locked, probably the game is running
      return false;
    } else {
      throw MOShared::os_error(
          QObject::tr("failed to access %1").arg(fileName).toUtf8().constData());
    }
  }
  CloseHandle(file);
}