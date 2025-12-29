#include "envprocess.h"
#include "multiprocess.h"

#include <QApplication>
#include <log.h>
#include <sys/mman.h>

// From https://doc.qt.io/qt-6/native-ipc-keys.html#ownership:
// On Unix systems, the Qt classes that created the object will be responsible for
// cleaning up the object in question. Therefore, if the application with that C++
// object exits uncleanly (a crash, qFatal(), etc.), the object may be left behind. If
// that happens, applications may fail to create the object again and should instead
// attach to an existing one.

// note: the shared memory object is located in a dedicated tmpfs filesystem that is
// normally mounted under /dev/shm, so manual clean-up can be achieved with `rm
// /dev/shm/<key>`

bool isOnlyMoProcess()
{
  const std::vector<env::Process> processes = env::getRunningProcesses();
  // appimages create 2 processes
  int count           = 0;
  const bool appImage = getenv("APPIMAGE") != nullptr;
  for (const auto& process : processes) {
    if (process.name() == QApplication::applicationName() &&
        std::cmp_not_equal(process.pid(), getpid())) {
      if (!appImage) {
        return false;
      }
      // return false if running as appimage and there is more than 1 other process
      if (count++ > 1) {
        return false;
      }
    }
  }

  return true;
}

MOMultiProcess::~MOMultiProcess()
{
  if (m_OwnsSM) {
    if (shm_unlink(m_SharedMem.nativeKey().toLocal8Bit()) == -1) {
      const int e = errno;
      MOBase::log::error("Error removing shm key '{}': {}", m_SharedMem.nativeKey(),
                         strerror(e));
    }
  }
}
