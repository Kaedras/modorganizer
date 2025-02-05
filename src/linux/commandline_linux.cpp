#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "loglist.h"
#include "messagedialog.h"
#include "multiprocess.h"
#include "organizercore.h"
#include "shared/appconfig.h"
#include "shared/util.h"
#include <log.h>
#include <report.h>

#include "stub.h"

namespace cl
{

using namespace MOBase;
int LaunchCommand::SpawnWaitProcess(LPCWSTR workingDirectory, LPCWSTR commandLine)
{
  STUB();
  return 0;
}

LPCWSTR LaunchCommand::UntouchedCommandLineArguments(int parseArgCount,
                                        std::vector<std::wstring>& parsedArgs)
{
  STUB();
  return nullptr;
}

}  // namespace cl