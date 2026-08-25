#include "multiprocess.h"
#include <atomic>
#include <cstring>
#include <semaphore.h>
#include <sys/mman.h>

// From https://doc.qt.io/qt-6/native-ipc-keys.html#ownership:
// On Unix systems, the Qt classes that created the object will be responsible for
// cleaning up the object in question. Therefore, if the application with that C++
// object exits uncleanly (a crash, qFatal(), etc.), the object may be left behind. If
// that happens, applications may fail to create the object again and should instead
// attach to an existing one.

// note: the shared memory object is located in a dedicated tmpfs filesystem that is
// normally mounted under /dev/shm, so manual clean-up can be achieved with
// `rm /dev/shm/<key>`

using namespace std;

static array<char, 256> s_shmKey{};
static atomic s_hasShmKey{false};

void MOMultiProcess::registerShmKey(const QString& key)
{
  const QByteArray bytes = key.toLocal8Bit();
  if (cmp_less(bytes.size(), s_shmKey.size())) {
    memcpy(s_shmKey.data(), bytes.constData(), bytes.size());
    s_shmKey[bytes.size()] = '\0';
    s_hasShmKey.store(true);
  } else {
    MOBase::log::error("shm key too large, size: {}, max: {}", bytes.size(),
                       s_shmKey.size());
  }
}

void MOMultiProcess::removeShm()
{
  if (s_hasShmKey.exchange(false)) {
    if (shm_unlink(s_shmKey.data()) == -1) {
      cerr << "shm_unlink error: " << strerror(errno) << '\n';
    }

    if (sem_unlink(s_shmKey.data()) == -1) {
      cerr << "sem_unlink error: " << strerror(errno) << '\n';
    }
  }
}

MOMultiProcess::~MOMultiProcess()
{
  if (m_OwnsSM) {
    removeShm();
  }
}
