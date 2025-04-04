#include "compatibility.h"
#include "env.h"
#include "envdump.h"
#include "envmetrics.h"
#include "envmodule.h"
#include "envshortcut.h"
#include "settings.h"
#include "shared/util.h"
#include "stub.h"
#include <log.h>
#include <sys/resource.h>
#include <utility.h>

using namespace std;
using namespace Qt::StringLiterals;

namespace env
{

using namespace MOBase;

Console::Console() : m_hasConsole(true), m_in(stdin), m_out(stdout), m_err(stderr) {}

Console::~Console() {}

ModuleNotification::~ModuleNotification() {}

std::unique_ptr<ModuleNotification>
Environment::onModuleLoaded(QObject* o, std::function<void(Module)> f)
{
  return std::make_unique<ModuleNotification>(o, f);
}

std::optional<QString> getAssocString(const QFileInfo& file)
{
  STUB_PARAM(file.absolutePath().toStdString());
  return {};
  // const auto ext = "."s + file.suffix().toStdString();
}

std::pair<QString, QString> splitExeAndArguments(const QString& cmd)
{
  qsizetype exeBegin = 0;
  qsizetype exeEnd   = -1;

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
    static const QRegularExpression regex("\\s");
    exeEnd = cmd.indexOf(regex);
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
QString processPath(HANDLE process = ::INVALID_HANDLE_VALUE)
{
  if (process == 0) {
    return {};
  }

  pid_t pid;
  if (process == ::INVALID_HANDLE_VALUE) {
    pid = getpid();
  } else {
    pid = pidfd_getpid(process);
    if (pid == -1) {
      return {};
    }
  }

  std::filesystem::path exe("/proc/" + std::to_string(pid) + "/exe");
  return QString::fromStdString(read_symlink(exe));
}

QString processFilename(HANDLE process = ::INVALID_HANDLE_VALUE)
{
  const auto p = processPath(process);
  if (p.isEmpty()) {
    return {};
  }

  return QFileInfo(p).fileName();
}

Association getAssociation(const QFileInfo& targetInfo)
{
  STUB_PARAM(targetInfo.absolutePath().toStdString());
  return {};
}

bool registryValueExists([[maybe_unused]] const QString& key,
                         [[maybe_unused]] const QString& value)
{
  return false;
}

void deleteRegistryKeyIfEmpty([[maybe_unused]] const QString& name) {}

bool createMiniDump(const QString& dir, HANDLE process, CoreDumpTypes type)
{
  // check if gcore is available
  if (QStandardPaths::findExecutable(u"gcore"_s).isEmpty()) {
    return false;
  }

  // set core limit to enable core dumps
  rlimit core_limits{RLIM_INFINITY, RLIM_INFINITY};
  setrlimit(RLIMIT_CORE, &core_limits);

  std::unique_ptr<QProcess> p = std::make_unique<QProcess>();
  p->setProgram(u"gcore"_s);
  p->setArguments({u"-d"_s, dir});
  p->start();

  bool result = p->waitForFinished();

  // todo: compress core dump

  // reset limits
  core_limits = {0, 0};
  setrlimit(RLIMIT_CORE, &core_limits);

  return result;
}

bool coredumpOther(CoreDumpTypes type)
{
  STUB();
  return {};
}

}  // namespace env
