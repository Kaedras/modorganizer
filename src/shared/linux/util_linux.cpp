#include "linux/stub.h"
#include "shared/util.h"
#include <pthread.h>

namespace MOShared
{

MOBase::Version createVersionInfo()
{
  STUB();
  return {0, 0, 1};
}
QString getUsvfsVersionString()
{
  STUB();
  return "0.0.1";
}

void SetThisThreadName(const QString& s)
{
  pthread_t self = pthread_self();
  pthread_setname_np(self, s.toStdString().c_str());
}

}  // namespace MOShared
