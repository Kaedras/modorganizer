#include "commandline.h"
#include "loglist.h"
#include "organizercore.h"
#include "shared/util.h"
#include <log.h>
#include <report.h>

namespace cl
{

using namespace MOBase;
int LaunchCommand::SpawnWaitProcess(nativeCString workingDirectory,
                                    nativeCString commandLine)
{
  char* pwd = get_current_dir_name();

  chdir(workingDirectory);
  int result = system(commandLine);

  chdir(pwd);
  free(pwd);

  if (result == -1) {
    const int e = errno;
    log::error("Error running command: {}, {}", commandLine, strerror(e));
  }

  return result;
}

}  // namespace cl
