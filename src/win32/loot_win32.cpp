#include "loot.h"
#include "json.h"
#include "lootdialog.h"
#include "organizercore.h"
#include "spawn.h"
#include <log.h>
#include <report.h>

using namespace MOBase;
using namespace json;

static QString LootReportPath  = QDir::temp().absoluteFilePath("lootreport.json");
static const DWORD PipeTimeout = 500;

class AsyncPipe
{
public:
  AsyncPipe() : m_ioPending(false)
  {
    std::fill(std::begin(m_buffer), std::end(m_buffer), 0);
    std::memset(&m_ov, 0, sizeof(m_ov));
  }

  env::HandlePtr create()
  {
    // creating pipe
    env::HandlePtr out(createPipe());
    if (out.get() == INVALID_HANDLE_VALUE) {
      return {};
    }

    HANDLE readEventHandle = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (readEventHandle == NULL) {
      const auto e = GetLastError();
      log::error("CreateEvent failed for loot, {}", formatSystemMessage(e));
      return {};
    }

    m_ov.hEvent = readEventHandle;
    m_readEvent.reset(readEventHandle);

    return out;
  }

  std::string read()
  {
    if (m_ioPending) {
      return checkPending();
    } else {
      return tryRead();
    }
  }

private:
  static const std::size_t bufferSize = 50000;

  env::HandlePtr m_stdout;
  env::HandlePtr m_readEvent;
  char m_buffer[bufferSize];
  OVERLAPPED m_ov;
  bool m_ioPending;

  HANDLE createPipe()
  {
    static const wchar_t* PipeName = L"\\\\.\\pipe\\lootcli_pipe";

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength             = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle      = TRUE;

    env::HandlePtr pipe;

    // creating pipe
    {
      HANDLE pipeHandle =
          ::CreateNamedPipe(PipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 50'000,
                            50'000, PipeTimeout, &sa);

      if (pipeHandle == INVALID_HANDLE_VALUE) {
        const auto e = GetLastError();
        log::error("CreateNamedPipe failed, {}", formatSystemMessage(e));
        return INVALID_HANDLE_VALUE;
      }

      pipe.reset(pipeHandle);
    }

    {
      // duplicating the handle to read from it
      HANDLE outputRead = INVALID_HANDLE_VALUE;

      const auto r =
          DuplicateHandle(GetCurrentProcess(), pipe.get(), GetCurrentProcess(),
                          &outputRead, 0, TRUE, DUPLICATE_SAME_ACCESS);

      if (!r) {
        const auto e = GetLastError();
        log::error("DuplicateHandle for pipe failed, {}", formatSystemMessage(e));
        return INVALID_HANDLE_VALUE;
      }

      m_stdout.reset(outputRead);
    }

    // creating handle to pipe which is passed to CreateProcess()
    HANDLE outputWrite = ::CreateFileW(PipeName, FILE_WRITE_DATA | SYNCHRONIZE, 0, &sa,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (outputWrite == INVALID_HANDLE_VALUE) {
      const auto e = GetLastError();
      log::error("CreateFileW for pipe failed, {}", formatSystemMessage(e));
      return INVALID_HANDLE_VALUE;
    }

    return outputWrite;
  }

  std::string tryRead()
  {
    DWORD bytesRead = 0;

    if (!::ReadFile(m_stdout.get(), m_buffer, bufferSize, &bytesRead, &m_ov)) {
      const auto e = GetLastError();

      switch (e) {
      case ERROR_IO_PENDING: {
        m_ioPending = true;
        break;
      }

      case ERROR_BROKEN_PIPE: {
        // broken pipe probably means lootcli is finished
        break;
      }

      default: {
        log::error("{}", formatSystemMessage(e));
        break;
      }
      }

      return {};
    }

    return {m_buffer, m_buffer + bytesRead};
  }

  std::string checkPending()
  {
    DWORD bytesRead = 0;

    const auto r = WaitForSingleObject(m_readEvent.get(), PipeTimeout);

    if (r == WAIT_FAILED) {
      const auto e = GetLastError();
      log::error("WaitForSingleObject in AsyncPipe failed, {}", formatSystemMessage(e));
      return {};
    }

    if (!::GetOverlappedResult(m_stdout.get(), &m_ov, &bytesRead, FALSE)) {
      const auto e = GetLastError();

      switch (e) {
      case ERROR_IO_INCOMPLETE: {
        break;
      }

      case WAIT_TIMEOUT: {
        break;
      }

      case ERROR_BROKEN_PIPE: {
        // broken pipe probably means lootcli is finished
        break;
      }

      default: {
        log::error("GetOverlappedResult failed, {}", formatSystemMessage(e));
        break;
      }
      }

      return {};
    }

    ::ResetEvent(m_readEvent.get());
    m_ioPending = false;

    return {m_buffer, m_buffer + bytesRead};
  }
};

extern log::Levels levelFromLoot(lootcli::LogLevels level);

bool Loot::start(QWidget* parent, bool didUpdateMasterList)
{
  deleteReportFile();

  log::debug("starting loot");

  m_pipe.reset(new AsyncPipe);

  env::HandlePtr stdoutHandle = m_pipe->create();
  if (!stdoutHandle) {
    return false;
  }

  // vfs
  m_core.prepareVFS();

  // spawning
  if (!spawnLootcli(parent, didUpdateMasterList, std::move(stdoutHandle))) {
    return false;
  }

  // starting thread
  log::debug("starting loot thread");
  m_thread.reset(QThread::create([&] {
    lootThread();
  }));
  m_thread->start();

  return true;
}

bool Loot::spawnLootcli(QWidget* parent, bool didUpdateMasterList,
                        env::HandlePtr stdoutHandle)
{
  const auto logLevel = m_core.settings().diagnostics().lootLogLevel();

  QStringList parameters;
  parameters << "--game" << m_core.managedGame()->lootGameName()

             << "--gamePath"
             << QString("\"%1\"").arg(
                    m_core.managedGame()->gameDirectory().absolutePath())

             << "--pluginListPath"
             << QString("\"%1/loadorder.txt\"").arg(m_core.profilePath())

             << "--logLevel"
             << QString::fromStdString(lootcli::logLevelToString(logLevel))

             << "--out" << QString("\"%1\"").arg(LootReportPath)

             << "--language" << m_core.settings().interface().language();

  if (didUpdateMasterList) {
    parameters << "--skipUpdateMasterlist";
  }

  spawn::SpawnParameters sp;
  sp.binary    = QFileInfo(qApp->applicationDirPath() + "/loot/lootcli.exe");
  sp.arguments = parameters.join(" ");
  sp.currentDirectory.setPath(qApp->applicationDirPath() + "/loot");
  sp.hooked = true;
  sp.stdOut = stdoutHandle.get();

  HANDLE lootHandle = spawn::startBinary(parent, sp);

  if (lootHandle == INVALID_HANDLE_VALUE) {
    emit log(log::Levels::Error, tr("failed to start loot"));
    return false;
  }

  m_lootProcess.reset(lootHandle);

  return true;
}

bool Loot::waitForCompletion()
{
  bool terminating = false;

  log::debug("loot thread waiting for completion on lootcli");

  for (;;) {
    DWORD res = WaitForSingleObject(m_lootProcess.get(), 100);

    if (res == WAIT_OBJECT_0) {
      log::debug("lootcli has completed");
      // done
      break;
    }

    if (res == WAIT_FAILED) {
      const auto e = GetLastError();
      log::error("failed to wait on loot process, {}", formatSystemMessage(e));
      return false;
    }

    if (m_cancel) {
      log::debug("terminating lootcli process");
      ::TerminateProcess(m_lootProcess.get(), 1);

      log::debug("waiting for loocli process to terminate");
      WaitForSingleObject(m_lootProcess.get(), INFINITE);

      log::debug("lootcli terminated");
      return false;
    }

    processStdout(m_pipe->read());
  }

  if (m_cancel) {
    return false;
  }

  processStdout(m_pipe->read());

  // checking exit code
  DWORD exitCode = 0;

  if (!::GetExitCodeProcess(m_lootProcess.get(), &exitCode)) {
    const auto e = GetLastError();
    log::error("failed to get exit code for loot, {}", formatSystemMessage(e));
    return false;
  }

  if (exitCode != 0UL) {
    emit log(log::Levels::Error,
             tr("Loot failed. Exit code was: 0x%1").arg(exitCode, 0, 16));
    return false;
  }

  return true;
}

void Loot::processStdout(const std::string& lootOut)
{
  emit output(QString::fromStdString(lootOut));

  m_outputBuffer += lootOut;
  if (m_outputBuffer.empty()) {
    return;
  }

  std::size_t start = 0;

  for (;;) {
    const auto newline = m_outputBuffer.find("\n", start);
    if (newline == std::string::npos) {
      break;
    }

    const std::string_view line(m_outputBuffer.c_str() + start, newline - start);
    const auto m = lootcli::parseMessage(line);

    if (m.type == lootcli::MessageType::None) {
      log::error("unrecognised loot output: '{}'", line);
      continue;
    }

    processMessage(m);

    start = newline + 1;
  }

  m_outputBuffer.erase(0, start);
}
