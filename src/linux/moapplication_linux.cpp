#include "../log.h"
#include "../moapplication.h"
#include "stub.h"

using namespace MOBase;
using namespace Qt::StringLiterals;

// runtime libraries are found using rpath or runpath
// readelf -d <file> can be used to check those
// export LD_DEBUG=libs for debugging
void addDllsToPath()
{
  STUB();
}
