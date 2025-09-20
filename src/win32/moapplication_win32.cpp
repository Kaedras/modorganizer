#include "../env.h"
#include "../moapplication.h"

// This adds the `dlls` directory to the path so the dlls can be found. How
// MO is able to find dlls in there is a bit convoluted:
//
// Dependencies on DLLs can be baked into an executable by passing a
// `manifestdependency` option to the linker. This can be done on the command
// line or with a pragma. Typically, the dependency will not be a hardcoded
// filename, but an assembly name, such as Microsoft.Windows.Common-Controls.
//
// When Windows loads the exe, it will look for this assembly in a variety of
// places, such as in the WinSxS folder, but also in the program's folder. It
// will look for `assemblyname.dll` or `assemblyname/assemblyname.dll` and try
// to load that.
//
// If these files don't exist, then the loader gets creative and looks for
// `assemblyname.manifest` and `assemblyname/assemblyname.manifest`. A manifest
// file is just an XML file that can contain a list of DLLs to load for this
// assembly.
//
// In MO's case, there's a `pragma` at the beginning of this file which adds
// `dlls` as an "assembly" dependency. This is a bit of a hack to just force
// the loader to eventually find `dlls/dlls.manifest`, which contains the list
// of all the DLLs MO requires to load.
//
// This file was handwritten in `modorganizer/src/dlls.manifest.qt5` and
// is copied and renamed in CMakeLists.txt into `bin/dlls/dlls.manifest`. Note
// that the useless and incorrect .qt5 extension is removed.
//
void addDllsToPath()
{
  const auto dllsPath =
      QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/dlls");

  QCoreApplication::setLibraryPaths(QStringList(dllsPath) +
                                    QCoreApplication::libraryPaths());

  env::prependToPath(dllsPath);
}
