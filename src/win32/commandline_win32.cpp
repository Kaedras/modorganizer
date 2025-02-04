#include "../commandline.h"

namespace cl
{

using namespace MOBase;


int LaunchCommand::SpawnWaitProcess(const QString& workingDirectory, const QString& commandLine)
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

// Parses the first parseArgCount arguments of the current process command line and
// returns them in parsedArgs, the rest of the command line is returned untouched.
QString
LaunchCommand::UntouchedCommandLineArguments(int parseArgCount,
                                             std::vector<QString>& parsedArgs)
{
  LPCWSTR cmd = GetCommandLineW();
  LPCWSTR arg = nullptr;  // to skip executable name
  for (; parseArgCount >= 0 && *cmd; ++cmd) {
    if (*cmd == '"') {
      int escaped = 0;
      for (++cmd; *cmd && (*cmd != '"' || escaped % 2 != 0); ++cmd)
        escaped = *cmd == '\\' ? escaped + 1 : 0;
    }
    if (*cmd == ' ') {
      if (arg)
        if (cmd - 1 > arg && *arg == '"' && *(cmd - 1) == '"')
          parsedArgs.push_back(std::wstring(arg + 1, cmd - 1));
        else
          parsedArgs.push_back(std::wstring(arg, cmd));
      arg = cmd + 1;
      --parseArgCount;
    }
  }
  return cmd;
}

}  // namespace cl