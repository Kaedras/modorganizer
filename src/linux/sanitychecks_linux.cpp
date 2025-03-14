#include "env.h"
#include "envmodule.h"
#include "sanitychecks.h"
#include "settings.h"
#include <iplugingame.h>
#include <log.h>
#include <utility.h>

#include "stub.h"

namespace sanity
{

using namespace MOBase;

std::vector<std::pair<QString, QString>> getSystemDirectories()
{
  STUB();
  return {};
}

int checkMicrosoftStore(const QDir& gameDir)
{
  (void)gameDir;
  return 0;
}

int checkIncompatibilities(const env::Environment& e)
{
  STUB();
  return 0;
}

int checkIncompatibleModule(const env::Module& m)
{
  STUB();
  return 0;
}

}  // namespace sanity