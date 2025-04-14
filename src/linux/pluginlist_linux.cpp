#include <QString>
#include <sys/file.h>
#include <uibase/linux/compatibility.h>

#include "../shared/os_error.h"

bool isFileLocked(const QString& fileName) noexcept(false)
{
  // open file descriptor
  FdCloser fd(open(fileName.toStdString().c_str(), O_RDWR | O_EXCL));
  if (fd) {
    const int e = errno;
    throw MOShared::os_error(
        QObject::tr("failed to access %1").arg(fileName).toUtf8().constData(), e);
  }

  // check if file has been locked using fcntl
  struct flock lock{F_WRLCK};
  fcntl(fd.get(), F_GETLK, &lock);
  if (lock.l_type != F_UNLCK) {
    return true;
  }

  // check if file has been locked using flock
  int result = flock(fd.get(), LOCK_EX | LOCK_NB);
  return result != 0;
}