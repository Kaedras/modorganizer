#ifndef ENVMODULE_LINUX_H
#define ENVMODULE_LINUX_H

#include "compatibility.h"

// copied from
// https://learn.microsoft.com/en-us/windows/win32/api/verrsrc/ns-verrsrc-vs_fixedfileinfo
typedef struct tagVS_FIXEDFILEINFO {
  DWORD dwSignature;
  DWORD dwStrucVersion;
  DWORD dwFileVersionMS;
  DWORD dwFileVersionLS;
  DWORD dwProductVersionMS;
  DWORD dwProductVersionLS;
  DWORD dwFileFlagsMask;
  DWORD dwFileFlags;
  DWORD dwFileOS;
  DWORD dwFileType;
  DWORD dwFileSubtype;
  DWORD dwFileDateMS;
  DWORD dwFileDateLS;
} VS_FIXEDFILEINFO;

namespace env
{
// compatibility class for HandlePtr
// TODO: this is a first draft
class PidfdCloser
{
public:
  PidfdCloser(int pidfd) { m_pidfd = pidfd; }

  ~PidfdCloser()
  {
    if (m_pidfd != -1) {
      close(m_pidfd);
    }
  }

  int get() { return m_pidfd; }

  int release()
  {
    int tmp = m_pidfd;
    m_pidfd = -1;
    return tmp;
  }

private:
  int m_pidfd = -1;
};

using HandlePtr = PidfdCloser;

}
#endif  // ENVMODULE_LINUX_H