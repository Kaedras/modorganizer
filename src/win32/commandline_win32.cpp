#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "linux/stub.h"
#include "loglist.h"
#include "messagedialog.h"
#include "multiprocess.h"
#include "organizercore.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include <log.h>
#include <report.h>

namespace cl
{

int LaunchCommand::SpawnWaitProcess(nativeCString workingDirectory,
                                    nativeCString commandLine)
{
  PROCESS_INFORMATION pi{0};
  STARTUPINFO si{0};
  si.cb                        = sizeof(si);
  std::wstring commandLineCopy = commandLine;

  if (!CreateProcessW(NULL, &commandLineCopy[0], NULL, NULL, FALSE, 0, NULL,
                      workingDirectory, &si, &pi)) {
    // A bit of a problem where to log the error message here, at least this way you can
    // get the message using a either DebugView or a live debugger:
    std::wostringstream ost;
    ost << L"CreateProcess failed: " << commandLine << ", " << GetLastError();
    OutputDebugStringW(ost.str().c_str());
    return -1;
  }

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitCode = (DWORD)-1;
  ::GetExitCodeProcess(pi.hProcess, &exitCode);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return static_cast<int>(exitCode);
}

}  // namespace cl
