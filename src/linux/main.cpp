#include "commandline.h"
#include "env.h"
#include "instancemanager.h"
#include "loglist.h"
#include "moapplication.h"
#include "multiprocess.h"
#include "organizercore.h"
#include "shared/util.h"
#include "thread_utils.h"
#include <QMessageBox>
#include <atomic>
#include <client/linux/handler/exception_handler.h>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <report.h>
#include <sys/prctl.h>

using namespace MOBase;
using namespace std;
using namespace Qt::StringLiterals;
using namespace google_breakpad;
namespace fs = std::filesystem;

namespace env
{
// defined in `env.cpp`
extern HandlePtr dumpFile(const QString& dir);
}  // namespace env

bool filterCallback(void* context);

namespace
{
set<pid_t> getChildPids(pid_t pid)
{
  set<pid_t> pids;
  string taskDirPath = "/proc/" + to_string(pid) + "/task";
  DIR* dir           = opendir(taskDirPath.c_str());
  if (!dir) {
    return pids;
  }

  while (dirent* entry = readdir(dir)) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    string childrenFilePath = taskDirPath + "/" + entry->d_name + "/children";
    ifstream file(childrenFilePath);
    pid_t p;
    while (file >> p) {
      pids.insert(p);
    }
  }

  closedir(dir);
  return pids;
}

void getAllDescendants(pid_t pid, set<pid_t>& allPids)
{
  const auto children = getChildPids(pid);
  for (pid_t child : children) {
    if (allPids.insert(child).second) {
      getAllDescendants(child, allPids);
    }
  }
}

void reap()
{
  static constexpr auto timeout = 100ms;

  prctl(PR_SET_CHILD_SUBREAPER, 1);
  const auto g = qScopeGuard([] {
    prctl(PR_SET_CHILD_SUBREAPER, 0);
  });

  set<pid_t> descendants;
  getAllDescendants(getpid(), descendants);

  if (descendants.empty()) {
    return;
  }

  for (pid_t pid : descendants) {
    kill(pid, SIGTERM);
  }

  const auto start = chrono::steady_clock::now();
  while (true) {
    pid_t result = waitpid(-1, nullptr, WNOHANG);
    if (result > 0) {
      descendants.erase(result);
      continue;
    }
    if (result == -1 && errno == ECHILD) {
      return;
    }
    if (chrono::steady_clock::now() - start >= timeout) {
      // send SIGKILL after timeout has passed
      for (pid_t pid : descendants) {
        kill(pid, SIGKILL);
      }
      while (wait(nullptr) > 0 || errno == EINTR) {
      }
      return;
    }
    this_thread::sleep_for(1ms);
  }
}

[[noreturn]] void RestartModOrganizer(int exitCode)
{
  MOMultiProcess::removeShm();

  // required to not leave zombie `QtWebEngineProcess` processes behind
  reap();

  const QByteArray pathBA = MOShared::getApplicationFilePath().toLocal8Bit();
  const char* path        = pathBA.constData();

  if (exitCode == ReselectExitCode) {
    execl(path, path, "--pick", nullptr);
  } else {
    execl(path, path, nullptr);
  }

  const int e = errno;
  log::error("exec error: {}", strerror(e));
  exit(1);
}
}  // namespace

static bool dumpCallback(const MinidumpDescriptor&, void*, bool succeeded)
{
  MOMultiProcess::removeShm();

  if (succeeded) {
    log::error("ModOrganizer has crashed, core dump created.");
  } else {
    log::error("ModOrganizer has crashed, core dump failed.");
  }
  return succeeded;
}

thread_local std::terminate_handler g_prevTerminateHandler = nullptr;
thread_local std::atomic<bool> g_signalHandlersInstalled{false};
thread_local QString g_coreDumpPath;
thread_local MinidumpDescriptor g_minidumpDescriptor(".");
thread_local ExceptionHandler g_exceptionHandler(g_minidumpDescriptor, filterCallback,
                                                 dumpCallback, nullptr, true, -1);

// the filter callback is used to set the minidump descriptor
bool filterCallback(void*)
{
  env::HandlePtr file = env::dumpFile(g_coreDumpPath);

  g_minidumpDescriptor = MinidumpDescriptor{file.release()};
  g_exceptionHandler.set_minidump_descriptor(g_minidumpDescriptor);

  return true;
}

int run(int argc, char* argv[]);

int main(int argc, char* argv[])
{
  const int r = run(argc, argv);
  std::cout << "mod organizer done\n";
  return r;
}

int run(int argc, char* argv[])
{
  MOShared::SetThisThreadName(u"main"_s);
  setExceptionHandlers();

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

  QStringList args;
  for (int i = 0; i < argc; i++) {
    QString arg = QString::fromLocal8Bit(argv[i]);
    // escape spaces
    arg.replace(' ', uR"(\ )"_s);
    args.append(std::move(arg));
  }
  if (auto r = cl.process(ToNativeString(args.join(' ')))) {
    return *r;
  }

  initLogging();

  // must be after logging
  TimeThis tt(u"main() multiprocess"_s);

  MOApplication app(argc, argv);

  app.setDesktopFileName(u"ModOrganizer"_s);

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
        RestartModOrganizer(r);
      } else if (r != 0) {
        // something failed, quit
        return r;
      }
    }

    // set core dump path after `MOApplication::setup()` has been called
    g_coreDumpPath = OrganizerCore::getGlobalCoreDumpPath();

    // check if the command line wants to run something right now
    if (auto r = cl.runPostOrganizer(app.core())) {
      return *r;
    }

    // run the main window
    const auto r = app.run(multiProcess);

    if (r == RestartExitCode || r == ReselectExitCode) {
      RestartModOrganizer(r);
    }

    return r;
  } catch (const std::exception& e) {
    reportError(e.what());
    return 1;
  }
}

void signalHandler(int sig)
{
  MOMultiProcess::removeShm();

  // clear the signal handler and raise the signal again
  struct sigaction sa = {};
  sa.sa_handler       = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sigaction(sig, &sa, nullptr);
  raise(sig);
}

void onTerminate() noexcept
{
  MOMultiProcess::removeShm();
  if (g_prevTerminateHandler) {
    g_prevTerminateHandler();
  } else {
    std::abort();
  }
}

void setExceptionHandlers()
{
  if (!g_prevTerminateHandler) {
    g_prevTerminateHandler = std::set_terminate(onTerminate);
  }

  if (!g_signalHandlersInstalled.exchange(true)) {
    struct sigaction sa = {};
    sa.sa_handler       = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
  }
}
