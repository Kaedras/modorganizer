#include "envmodule.h"
#include "env.h"
#include "stub.h"

#include "compatibility.h"
#include "envdump.h"
#include "envmetrics.h"
#include "envshortcut.h"
#include "settings.h"
#include "shared/util.h"
#include <log.h>
#include <utility.h>

using namespace std;

namespace env
{

using namespace MOBase;

Console::Console() : m_hasConsole(true), m_in(stdin), m_out(stdout), m_err(stderr)
{
}

Console::~Console()
{
}

ModuleNotification::~ModuleNotification(){}

std::unique_ptr<ModuleNotification> Environment::onModuleLoaded(QObject* o,
    std::function<void(Module)> f)
{
  return std::make_unique<ModuleNotification>(o, f);
}

std::optional<QString> getAssocString(const QFileInfo& file)
{
  STUB();
  return {};
  // const auto ext = "."s + file.suffix().toStdString();
}

std::pair<QString, QString> splitExeAndArguments(const QString& cmd)
{
  int exeBegin = 0;
  int exeEnd   = -1;

  if (cmd[0] == '"') {
    // surrounded by double-quotes, so find the next one
    exeBegin = 1;
    exeEnd   = cmd.indexOf('"', exeBegin);

    if (exeEnd == -1) {
      log::error("missing terminating double-quote in command line '{}'", cmd);
      return {};
    }
  } else {
    // no double-quotes, find the first whitespace
    exeEnd = cmd.indexOf(QRegularExpression("\\s"));
    if (exeEnd == -1) {
      exeEnd = cmd.size();
    }
  }

  QString exe  = cmd.mid(exeBegin, exeEnd - exeBegin).trimmed();
  QString args = cmd.mid(exeEnd + 1).trimmed();

  return {std::move(exe), std::move(args)};
}

// returns the filename of the given process or the current one
//
QString processPath(HANDLE process = INVALID_HANDLE_VALUE)
{
  if (process == 0) {
    return {};
  }
  std::filesystem::path exe("/proc/" + std::to_string(process) + "/exe");
  return QString::fromStdString(read_symlink(exe));
}

QString processFilename(HANDLE process = INVALID_HANDLE_VALUE)
{
  const auto p = processPath(process);
  if (p.isEmpty()) {
    return {};
  }

  return QFileInfo(p).fileName();
}

Association getAssociation(const QFileInfo& targetInfo)
{
  STUB();
  return {};
}

bool registryValueExists(const QString& key, const QString& value)
{
  STUB();
  return false;
}

void deleteRegistryKeyIfEmpty(const QString& name)
{
  STUB();
}

bool createMiniDump(const QString& dir, HANDLE process, CoreDumpTypes type)
{
  STUB();
  return true;
}

bool coredumpOther(CoreDumpTypes type)
{
  STUB();
  return {};
}

}  // namespace env
