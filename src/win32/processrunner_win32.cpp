#include "env.h"
#include "envmodule.h"
#include "instancemanager.h"
#include "iuserinterface.h"
#include "organizercore.h"
#include "processrunner.h"
#include <iplugingame.h>
#include <log.h>
#include <report.h>

using namespace MOBase;

extern void waitForProcessesThread(ProcessRunner::Results& result, HANDLE job,
                                   UILocker::Session* ls, std::atomic<bool>& interrupt);

std::optional<ProcessRunner::Results> singleWait(HANDLE handle, DWORD pid)
{
  if (handle == INVALID_HANDLE_VALUE) {
    return ProcessRunner::Error;
  }

  const auto res = WaitForSingleObject(handle, 50);

  switch (res) {
  case WAIT_OBJECT_0: {
    log::debug("process {} completed", pid);
    return ProcessRunner::Completed;
  }

  case WAIT_TIMEOUT: {
    // still running
    return {};
  }

  case WAIT_FAILED:  // fall-through
  default: {
    // error
    const auto e = ::GetLastError();
    log::error("failed waiting for {}, {}", pid, formatSystemMessage(e));
    return ProcessRunner::Error;
  }
  }
}

ProcessRunner::Results waitForProcesses(const std::vector<HANDLE>& initialProcesses,
                                        UILocker::Session* ls)
{
  if (initialProcesses.empty()) {
    // nothing to wait for
    return ProcessRunner::Completed;
  }

  // using a job so any child process started by any of those processes can also
  // be captured and monitored
  env::HandlePtr job(CreateJobObjectW(nullptr, nullptr));
  if (!job) {
    const auto e = GetLastError();

    log::error("failed to create job to wait for processes, {}",
               formatSystemMessage(e));

    return ProcessRunner::Error;
  }

  bool oneWorked = false;

  for (auto&& h : initialProcesses) {
    if (::AssignProcessToJobObject(job.get(), h)) {
      oneWorked = true;
    } else {
      const auto e = GetLastError();

      // this happens when closing MO while multiple processes are running,
      // so the logging is disabled until it gets fixed

      // log::error(
      //  "can't assign process to job to wait for processes, {}",
      //  formatSystemMessage(e));

      // keep going
    }
  }

  HANDLE monitor = INVALID_HANDLE_VALUE;

  if (oneWorked) {
    monitor = job.get();
  } else {
    // none of the handles could be added to the job, just monitor the first one
    monitor = initialProcesses[0];
  }

  auto results = ProcessRunner::Running;
  std::atomic<bool> interrupt(false);

  auto* t = QThread::create(waitForProcessesThread, std::ref(results), monitor, ls,
                            std::ref(interrupt));

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
    if (!::GetExitCodeProcess(initialProcess, exitCode)) {
      const auto e = ::GetLastError();
      log::warn("failed to get exit code of process, {}", formatSystemMessage(e));
    }
  }

  return r;
}
