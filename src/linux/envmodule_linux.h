#ifndef ENVMODULE_LINUX_H
#define ENVMODULE_LINUX_H

#include "compatibility.h"

// copied from
// https://learn.microsoft.com/en-us/windows/win32/api/verrsrc/ns-verrsrc-vs_fixedfileinfo
typedef struct tagVS_FIXEDFILEINFO
{
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
class FdCloser
{
public:
  explicit FdCloser(int fd) : m_fd(fd) {}
  FdCloser() = default;

  FdCloser& operator=(int fd)
  {
    if (m_fd != -1) {
      close(m_fd);
    }
    m_fd = fd;

    return *this;
  }

  operator bool() const noexcept
  {
    return m_fd != -1;
  }

  ~FdCloser()
  {
    if (m_fd != -1) {
      close(m_fd);
    }
  }

  int get() const { return m_fd; }

  int release()
  {
    int tmp = m_fd;
    m_fd = -1;
    return tmp;
  }

  bool isValid() const { return m_fd != -1; }

private:
  int m_fd = -1;
};

using HandlePtr = FdCloser;

}  // namespace env
#endif  // ENVMODULE_LINUX_H