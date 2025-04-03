#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "loglist.h"
#include "messagedialog.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"
#include <log.h>
#include <report.h>
#include <sys/resource.h>

using namespace MOBase;

// 1 GiB limit, compressed size will be much lower
static constexpr rlim_t coreDumpSizeLimit = 1024 * 1024 * 1024;
// anonymous private mappings
static constexpr int coreDumpFilter = 0b000'0001;

thread_local std::terminate_handler g_prevTerminateHandler = nullptr;

int run(int argc, char* argv[]);

int main(int argc, char* argv[])
{
  // let the kernel handle core dumps for now

  // set core dump limit to enable crash dumps
  // currently the produced core dump will be ~600 MiB, bz2 compression reduces the size
  // to ~15 MiB
  rlimit core_limits{coreDumpSizeLimit, coreDumpSizeLimit};
  setrlimit(RLIMIT_CORE, &core_limits);

  // set core dump filter
  std::ofstream filter("/proc/self/coredump_filter");
  if (filter.is_open()) {
    filter << coreDumpFilter << std::endl;
    filter.close();
  } else {
    const int e = errno;
    log::warn("Error writing coredump_filter, {}. Kernel may not be built with "
              "CONFIG_ELF_CORE.",
              strerror(e));
  }

  // todo: clean up old crash dumps, move them into crashDumps folder

  // todo: test with systemd (coredumpctl)

  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  MOShared::SetThisThreadName("main");
  setExceptionHandlers();
  // setExceptionHandlers();

  cl::CommandLine cl;

  QString str;
  for (int i = 0; i < argc; i++) {
    str.append(argv[i]).append(' ');
  }
  if (auto r = cl.process(str.toStdWString())) {
    return *r;
  }

  initLogging();

  // must be after logging
  TimeThis tt("main() multiprocess");

  MOApplication app(argc, argv);

  // check if the command line wants to run something right now
  if (auto r = cl.runPostApplication(app)) {
    return *r;
  }

  // check if there's another process running
  MOMultiProcess multiProcess(cl.multiple());

  if (multiProcess.ephemeral()) {
    // this is not the primary process

    if (cl.forwardToPrimary(multiProcess)) {
      // but there's something on the command line that could be forwarded to
      // it, so just exit
      return 0;
    }

    QMessageBox::information(
        nullptr, QObject::tr("Mod Organizer"),
        QObject::tr("An instance of Mod Organizer is already running"));

    return 1;
  }

  // check if the command line wants to run something right now
  if (auto r = cl.runPostMultiProcess(multiProcess)) {
    return *r;
  }

  tt.stop();

  // stuff that's done only once, even if MO restarts in the loop below
  app.firstTimeSetup(multiProcess);

  // force the "Select instance" dialog on startup, only for first loop or when
  // the current instance cannot be used
  bool pick = cl.pick();

  // MO runs in a loop because it can be restarted in several ways, such as
  // when switching instances or changing some settings
  for (;;) {
    try {
      auto& m = InstanceManager::singleton();

      if (cl.instance()) {
        m.overrideInstance(*cl.instance());
      }

      if (cl.profile()) {
        m.overrideProfile(*cl.profile());
      }

      // set up plugins, OrganizerCore, etc.
      {
        const auto r = app.setup(multiProcess, pick);
        pick         = false;

        if (r == RestartExitCode || r == ReselectExitCode) {
          // resets things when MO is "restarted"
          app.resetForRestart();

          // don't reprocess command line
          cl.clear();

          if (r == ReselectExitCode) {
            pick = true;
          }

          continue;
        } else if (r != 0) {
          // something failed, quit
          return r;
        }
      }

      // check if the command line wants to run something right now
      if (auto r = cl.runPostOrganizer(app.core())) {
        return *r;
      }

      // run the main window
      const auto r = app.run(multiProcess);

      if (r == RestartExitCode) {
        // resets things when MO is "restarted"
        app.resetForRestart();

        // don't reprocess command line
        cl.clear();

        continue;
      }

      return r;
    } catch (const std::exception& e) {
      reportError(e.what());
      return 1;
    }
  }
}

void onTerminate() noexcept
{
  const auto path = OrganizerCore::getGlobalCoreDumpPath();
  const auto type = OrganizerCore::getGlobalCoreDumpType();

  const auto r = env::coredump(path.isEmpty() ? QString() : path, type);

  if (r) {
    log::error("ModOrganizer has crashed, core dump created.");
  } else {
    log::error("ModOrganizer has crashed, core dump failed");
  }
  try {
    throw;
  } catch (const std::exception& e) {
    log::error(e.what());
  } catch (...) {
  }
}

void setExceptionHandlers()
{
  if (g_prevTerminateHandler) {
    // already called
    return;
  }

  g_prevTerminateHandler = std::set_terminate(onTerminate);
}
