#ifndef COMPATIBILITY_H
#define COMPATIBILITY_H

#include <cerrno>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>

extern "C"
{
#include <sys/pidfd.h>
}

// use pidfd instead of handle
using HANDLE     = int;
using DWORD      = uint32_t;
using SYSTEMTIME = timespec;
using LPCWSTR    = const wchar_t*;

// pidfd_open returns -1 on error
static inline constexpr int INVALID_HANDLE_VALUE = -1;
static inline constexpr auto ERROR_ACCESS_DENIED = EACCES;
static inline constexpr auto ERROR_CANCELLED     = ECANCELED;

inline int GetLastError()
{
  return errno;
}

inline DWORD GetProcessId(HANDLE h)
{
  return pidfd_getpid(h);
}

inline HANDLE GetCurrentProcess()
{
  return pidfd_open(getpid(), 0);
}

inline DWORD GetCurrentProcessId()
{
  return getpid();
}

inline bool CloseHandle(HANDLE hObject)
{
  return close(hObject) == 0;
}

inline int NtClose(HANDLE Handle)
{
  return close(Handle);
}

inline DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
  pollfd pfd = {hHandle, POLLIN, 0};
  return poll(&pfd, 1, dwMilliseconds);
}

#endif  // COMPATIBILITY_H
