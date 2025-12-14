#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "loglist.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"
#include <client/linux/handler/exception_handler.h>
#include <log.h>
#include <report.h>
#include <sys/prctl.h>
#include <sys/resource.h>

using namespace MOBase;
using namespace std;
namespace fs = std::filesystem;

namespace env
{
extern HandlePtr dumpFile(const QString& dir);
}  // namespace env

static bool dumpCallback(const google_breakpad::MinidumpDescriptor& descriptor, void*,
                         bool succeeded)
{
  if (succeeded) {
    auto h = env::dumpFile(OrganizerCore::getGlobalCoreDumpPath());

    // get filename from fd
    error_code ec;
    fs::path filename = fs::read_symlink("/proc/self/fd/" + to_string(h.get()), ec);
    if (ec) {
      cerr << "Error getting filename from fd, " << ec.message() << "\n";
      return succeeded;
    }

    // rename file
    fs::rename(descriptor.path(), filename, ec);
    if (ec) {
      cerr << "Error renaming minidump, " << ec.message() << "\n";
      return succeeded;
    }

    // check if the crash dump path is set
    string path = OrganizerCore::getGlobalCoreDumpPath().toStdString();
    if (!path.empty()) {
      fs::create_directory(path);
      fs::rename(filename, path / filename, ec);
      if (ec) {
        cerr << "Error moving minidump to " << path << ", " << ec.message() << "\n";
      }
    }

  } else {
    cerr << "Error creating minidump\n";
  }
  return succeeded;
}

thread_local std::terminate_handler g_prevTerminateHandler = nullptr;

int run(int argc, char* argv[]);

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  MOShared::SetThisThreadName("main");
  google_breakpad::MinidumpDescriptor descriptor(".");
  google_breakpad::ExceptionHandler eh(descriptor, nullptr, dumpCallback, nullptr, true,
                                       -1);

  // allow ptrace from any process. required for crashdump command
  if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) == -1) {
    const int e = errno;
    log::warn("Error in prctl(PR_SET_PTRACER), {}", strerror(e));
  }

  rlimit limit;
  if (getrlimit(RLIMIT_CORE, &limit) == -1) {
    const int e = errno;
    log::error("getrlimit failed, {}", strerror(e));
    return 1;
  }
  limit.rlim_cur = RLIM_INFINITY;

  if (setrlimit(RLIMIT_CORE, &limit) == -1) {
    const int e = errno;
    log::error("setrlimit failed, {}", strerror(e));
    return 1;
  }

  cl::CommandLine cl;

  QString str;
  for (int i = 0; i < argc; i++) {
    str.append(argv[i]).append(' ');
  }
  if (auto r = cl.process(ToNativeString(str))) {
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

      // value should exist by now
      fs::path path = OrganizerCore::getGlobalCoreDumpPath().toStdString();
      if (!path.empty()) {
        descriptor = google_breakpad::MinidumpDescriptor(path);
        eh.set_minidump_descriptor(descriptor);

        // move old crash dumps into the crash dump folder
        for (auto item : fs::directory_iterator(".")) {
          if (item.path().extension() == ".dmp") {
            fs::copy(item.path(), path / item.path().filename());
            fs::remove(item.path());
          }
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

void setExceptionHandlers() {}
