#include "env.h"
#include "envmodule.h"
#include "instancemanager.h"
#include "iuserinterface.h"
#include "organizercore.h"
#include "processrunner.h"
#include <iplugingame.h>
#include <log.h>
#include <report.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace MOBase;
using namespace std;

std::optional<ProcessRunner::Results> singleWait(HANDLE pidFd, DWORD pid)
{
  if (pidFd == -1) {
    return ProcessRunner::Error;
  }

  // todo: find a better solution
  // poll() supports timeouts, but is unreliable here
  this_thread::sleep_for(50ms);

  siginfo_t info{};
  int result = waitid(P_PID, pid, &info, WEXITED | WSTOPPED | WNOHANG | WNOWAIT);

  if (result == 0) {
    if (info.si_pid != 0) {
      log::debug("process {} completed", pid);
      return ProcessRunner::Completed;
    }

    // still running
    return {};
  }

  const int e = errno;
  log::error("failed waiting for {}, {}", pid, formatSystemMessage(e));
  return ProcessRunner::Error;
}

extern void waitForProcessesThread(ProcessRunner::Results& result, HANDLE job,
                                   UILocker::Session* ls, std::atomic<bool>& interrupt);

ProcessRunner::Results waitForProcesses(const std::vector<HANDLE>& initialProcesses,
                                        UILocker::Session* ls)
{
  if (initialProcesses.empty()) {
    // nothing to wait for
    return ProcessRunner::Completed;
  }

  auto results = ProcessRunner::Running;
  std::atomic<bool> interrupt(false);

  auto* t = QThread::create(waitForProcessesThread, std::ref(results),
                            initialProcesses.front(), ls, std::ref(interrupt));

  QEventLoop events;
  QObject::connect(t, &QThread::finished, [&] {
    events.quit();
  });

  t->start();
  events.exec();

  if (t->isRunning()) {
    interrupt = true;
    t->wait();
  }

  delete t;

  return results;
}

ProcessRunner::Results waitForProcess(HANDLE initialProcess, LPDWORD exitCode,
                                      UILocker::Session* ls)
{
  std::vector<HANDLE> processes = {initialProcess};

  const auto r = waitForProcesses(processes, ls);

  // as long as it's not running anymore, try to get the exit code
  if (exitCode && r != ProcessRunner::Running) {
    siginfo_t info;

    if (waitid(P_PIDFD, initialProcess, &info, WEXITED) != 0) {
      const auto e = errno;
      log::warn("failed to get exit code of process, {}", strerror(e));
    }
    *exitCode = info.si_code;
  }

  // wait for unmount to complete
  // todo: improve or remove this
  log::debug("sleeping for 10ms");
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  return r;
}
