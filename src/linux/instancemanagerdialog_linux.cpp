#include "instancemanager.h"
#include "instancemanagerdialog.h"

#include <utility.h>

using namespace MOBase;

void InstanceManagerDialog::explorePrefixDirectory() const
{
  if (const auto* i = singleSelection()) {
    shell::Explore(i->prefixDirectory());
  }
}
