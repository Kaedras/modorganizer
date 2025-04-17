#include "env.h"
#include "envos.h"
#include "envsecurity.h"
#include "overlayfs/overlayfsmanager.h"
#include "settings.h"
#include "shared/util.h"
#include "spawn.h"
#include "stub.h"
#include <QMessageBox>
#include <QString>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/steamutility.h>
#include <uibase/utility.h>

// undefine signals from qtmetamacros.h because it conflicts with glib
#ifdef signals
#undef signals
#endif
#include <flatpak/flatpak.h>

using namespace MOBase;
using namespace MOShared;
using namespace std;
using namespace Qt::StringLiterals;

static const QString steamFlatpak = u"com.valvesoftware.Steam"_s;

namespace spawn::dialogs
{

extern void spawnFailed(QWidget* parent, const SpawnParameters& sp, DWORD code);
extern void helperFailed(QWidget* parent, DWORD code, const QString& why,
                         const QString& binary, const QString& cwd,
                         const QString& args);

std::string makeRightsDetails(const QFileInfo& info)
{
  string s;

  auto permissions = info.permissions();

  int user   = 0;
  int group  = 0;
  int others = 0;

  if (permissions.testFlag(QFileDevice::ReadOwner)) {
    s += "r";
    user += 4;
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::WriteOwner)) {
    s += "w";
    user += 2;
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::ExeOwner)) {
    s += "x";
    user += 1;
  } else {
    s += "-";
  }

  if (permissions.testFlag(QFileDevice::ReadGroup)) {
    s += "r";
    group += 4;
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::WriteGroup)) {
    s += "w";
    group += 2;
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::ExeGroup)) {
    s += "x";
    group += 1;
  } else {
    s += "-";
  }

  if (permissions.testFlag(QFileDevice::ReadOther)) {
    s += "r";
    others += 4;
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::WriteOther)) {
    s += "w";
    others += 2;
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::ExeOther)) {
    s += "x";
    others += 1;
  } else {
    s += "-";
  }

  // append octal representation
  s += format(" ({}{}{})", user, group, others);

  return s;
}

QString makeDetails(const SpawnParameters& sp, DWORD code, const QString& more = {})
{
  std::string owner, rights;

  if (sp.binary.isFile()) {
    const auto fs = env::getFileSecurity(sp.binary.absoluteFilePath());

    if (fs.error.isEmpty()) {
      owner  = fs.owner.toStdString();
      rights = makeRightsDetails(sp.binary);
    } else {
      owner  = fs.error.toStdString();
      rights = fs.error.toStdString();
    }
  } else {
    owner  = "(file not found)";
    rights = "(file not found)";
  }

  const bool cwdExists =
      (sp.currentDirectory.isEmpty() ? true : sp.currentDirectory.exists());

  std::string elevated;
  if (auto b = env::Environment().getOsInfo().isElevated()) {
    elevated = (*b ? "yes" : "no");
  } else {
    elevated = "(not available)";
  }

  auto s = std::format(
      "Error {} {}{}: {}\n"
      " . binary: '{}'\n"
      " . owner: {}\n"
      " . rights: {}\n"
      " . arguments: '{}'\n"
      " . cwd: '{}'{}\n"
      " . hooked: {}\n"
      " . MO elevated: {}",
      code, strerror(static_cast<int>(code)), (more.isEmpty() ? more : ", " + more),
      formatSystemMessage(static_cast<int>(code)),
      QDir::toNativeSeparators(sp.binary.absoluteFilePath()).toStdString(), owner,
      rights, sp.arguments,
      QDir::toNativeSeparators(sp.currentDirectory.absolutePath()).toStdString(),
      (cwdExists ? "" : " (not found)"), (sp.hooked ? "yes" : "no"), elevated);

  return QString::fromStdString(s);
}

QString makeContent(const SpawnParameters& sp, DWORD code, const QString& message = {})
{
  STUB();
  return {strerror(static_cast<int>(code))};
}

QMessageBox::StandardButton badSteamPath(QWidget* parent)
{
  const auto details = QStringLiteral(
      "Can't start steam because it was not found. Tried PATH and flatpak");

  log::error("{}", details);

  return MOBase::TaskDialog(parent, QObject::tr("Cannot start Steam"))
      .main(QObject::tr("Cannot start Steam"))
      .content(
          QObject::tr("The path to the Steam executable cannot be found. You might try "
                      "reinstalling Steam."))
      .details(details)
      .icon(QMessageBox::Critical)
      .button({QObject::tr("Continue without starting Steam"),
               QObject::tr("The program may fail to launch."), QMessageBox::Yes})
      .button({QObject::tr("Cancel"), QMessageBox::Cancel})
      .exec();
}

bool confirmRestartAsAdmin(QWidget* parent, const SpawnParameters& sp)
{
  STUB();
  return false;
}

}  // namespace spawn::dialogs

namespace spawn
{
extern QString makeSteamArguments(const QString& username, const QString& password);

DWORD spawn(const SpawnParameters& sp, HANDLE& processHandle)
{
  if (sp.hooked) {
    if (!OverlayFsManager::getInstance().mount()) {
      return -1;
    }
  }

  pid_t pid = fork();

  // error
  if (pid == -1) {
    const int e = errno;
    log::error("error running {}, {}", sp.binary.absoluteFilePath().toStdString(),
               strerror(e));
    return e;
  }

  // child
  if (pid == 0) {
    // create argument list for execv
    vector<const char*> args = {sp.binary.absoluteFilePath().toLocal8Bit()};
    QStringList spArgs       = QProcess::splitCommand(sp.arguments);
    for (const auto& arg : spArgs) {
      args.push_back(arg.toLocal8Bit());
    }
    // array must be terminated by a null pointer
    args.push_back(nullptr);

    chdir(sp.binary.absolutePath().toLocal8Bit());
    execv(args[0], const_cast<char* const*>(args.data()));

    // exec only returns on error
    const int e = errno;
    log::error("error running {}, {}", sp.binary.absoluteFilePath().toStdString(),
               strerror(e));
    return e;
  }

  // parent
  int result = pidfd_open(pid, 0);
  if (result == -1) {
    const int e = errno;
    log::error("error getting pidfd for pid {}, {}", pid, strerror(e));
    return e;
  }
  processHandle = result;
  return 0;
}

int spawnProton(const SpawnParameters& sp, HANDLE& pidFd)
{
  // check steam path first to fail early if it is not found
  QString steamPath = findSteamCached();
  // QString steamPath = "/home/michael/.local/share/Steam";
  if (steamPath.isEmpty()) {
    log::error("could not find steam installation path");
    return -1;
  }

  if (sp.hooked) {
    if (!OverlayFsManager::getInstance().mount()) {
      return -1;
    }
  }

  // appID is required to get the proton version to use as well as the compatdata path
  if (sp.steamAppID.isEmpty()) {
    log::error("cannot run proton application {} because appid is empty",
               sp.binary.path().toStdString());
    return -1;
  }

  // command is
  // STEAM_COMPAT_DATA_PATH=compatdata/<appid>
  // STEAM_COMPAT_CLIENT_INSTALL_PATH=<steam path>
  // path/to/proton run application.exe

  // application is located at steamapps/common/<appliation>
  // compatdata is located at steamapps/compatdata/<appid>

  QString compatData = findCompatDataByAppID(sp.steamAppID);

  QString proton = findProtonByAppID(sp.steamAppID);
  if (proton.isEmpty()) {
    return -1;
  }

  // create argument list for execv
  vector<const char*> args = {proton.toLocal8Bit(), "run",
                              sp.binary.absoluteFilePath().toLocal8Bit()};
  QStringList spArgs       = QProcess::splitCommand(sp.arguments);
  for (const auto& arg : spArgs) {
    args.push_back(arg.toLocal8Bit());
  }
  // array must be terminated by a null pointer
  args.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0) {
    // child
    setenv("STEAM_COMPAT_DATA_PATH", compatData.toLocal8Bit(), 1);
    setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath.toLocal8Bit(), 1);
    chdir(sp.binary.absolutePath().toLocal8Bit());

    execv(args[0], const_cast<char* const*>(args.data()));
  } else {
    // parent
    int result = pidfd_open(pid, 0);
    if (result == -1) {
      const int e = errno;
      log::error("error getting pidfd for pid {}, {}", pid, strerror(e));
      return e;
    }
    pidFd = result;
    return 0;
  }

  const int e = errno;
  log::error("error running {}, {}", proton.toStdString(), strerror(e));
  return e;
}

bool restartAsAdmin(QWidget* parent)
{
  STUB();
  return false;
}

void startBinaryAdmin(QWidget* parent, const SpawnParameters& sp)
{
  if (!dialogs::confirmRestartAsAdmin(parent, sp)) {
    log::debug("user declined");
    return;
  }

  log::info("restarting MO as administrator");
  restartAsAdmin(parent);
}

std::pair<QString, int> getSteamExecutable(QWidget* parent)
{
  // try PATH
  QString steam = QStandardPaths::findExecutable(u"steam"_s);
  if (!steam.isEmpty()) {
    return {steam, 0};
  }

  // try flatpak
  // get flatpak installation
  GError* e                         = nullptr;
  FlatpakInstallation* installation = flatpak_installation_new_user(nullptr, &e);
  if (e != nullptr) {
    g_error_free(e);
    return {{}, dialogs::badSteamPath(parent)};
  }

  // get installation reference to steam
  FlatpakInstalledRef* flatpakInstalledRef = flatpak_installation_get_installed_ref(
      installation, FLATPAK_REF_KIND_APP, "com.valvesoftware.Steam", nullptr, "stable",
      nullptr, &e);
  if (e != nullptr || flatpakInstalledRef == nullptr) {
    // error or not found
    if (e != nullptr) {
      log::error("error getting steam flatpak location, {}", e->message);
      g_error_free(e);
    }
    return {{}, dialogs::badSteamPath(parent)};
  }

  return {steamFlatpak, 0};
}

bool startSteam(QWidget* parent)
{
  QString binary = getSteamExecutable(parent).first;
  if (binary.isEmpty()) {
    return dialogs::badSteamPath(parent) == QMessageBox::Yes;
  }
  QString arguments;

  // See if username and password supplied. If so, pass them into steam.
  QString username, password;
  if (Settings::instance().steam().login(username, password)) {
    if (username.length() > 0)
      MOBase::log::getDefault().addToBlacklist(username.toStdString(),
                                               "STEAM_USERNAME");
    if (password.length() > 0)
      MOBase::log::getDefault().addToBlacklist(password.toStdString(),
                                               "STEAM_PASSWORD");
    arguments = makeSteamArguments(username, password);
  }

  log::debug("starting steam process:\n"
             " . program: '{}'\n"
             " . username={}, password={}",
             binary.toStdString(), (username.isEmpty() ? "no" : "yes"),
             (password.isEmpty() ? "no" : "yes"));

  if (binary == steamFlatpak) {
    // run steam as flatpak
    GError* e;
    FlatpakInstallation* installation = flatpak_installation_new_user(nullptr, &e);
    if (e != nullptr) {
      log::error("error starting steam flatpak, {}", e->message);
      g_error_free(e);
      return false;
    }

    if (flatpak_installation_launch(installation, steamFlatpak.toStdString().c_str(),
                                    nullptr, "stable", nullptr, nullptr, &e)) {
      return true;
    }
    if (e) {
      log::error("error starting steam flatpak, {}", e->message);
      g_error_free(e);
    }
    return false;
  }

  QProcess p;
  p.setProgram(binary);
  p.setArguments(QProcess::splitCommand(arguments));
  return p.startDetached();
}

HANDLE startBinary(QWidget* parent, const SpawnParameters& sp)
{
  HANDLE handle = ::INVALID_HANDLE_VALUE;
  int e;
  if (sp.binary.suffix() == "exe"_L1) {
    e = spawnProton(sp, handle);
  } else {
    e = spawn(sp, handle);
  }

  switch (e) {
  case 0: {
    return handle;
  }

  case EACCES: {
    startBinaryAdmin(parent, sp);
    return ::INVALID_HANDLE_VALUE;
  }

  default: {
    dialogs::spawnFailed(parent, sp, e);
    return ::INVALID_HANDLE_VALUE;
  }
  }
}

QString findJavaInstallation(const QString& jarFile)
{
  (void)jarFile;

  auto s = getenv("JAVA_HOME");
  if (s != nullptr) {
    QString tmp = QString::fromUtf8(s);
    if (!tmp.endsWith('/')) {
      tmp += '/';
    }
    return tmp % u"bin/java"_s;
  }

  // not found
  return {};
}

bool isBatchFile(const QFileInfo& target)
{
  return target.suffix() == "sh"_L1;
}

bool isLinuxExecutable(const QFileInfo& target)
{
  // just treat everything that is not a directory and has executable permissions as
  // executable, this should also work for shell scripts
  return target.isExecutable() && !target.isDir();
}

bool isWindowsExecutable(const QFileInfo& target)
{
  return target.suffix() == "exe"_L1;
}

bool isExecutable(const QFileInfo& target)
{
  return isLinuxExecutable(target) || isWindowsExecutable(target);
}

bool isExeFile(const QFileInfo& target)
{
  return isExecutable(target);
}

extern bool isJavaFile(const QFileInfo& target);

FileExecutionContext getFileExecutionContext(QWidget* parent, const QFileInfo& target)
{
  if (isJavaFile(target)) {
    return {QFileInfo(u"java"_s),
            QStringLiteral(R"(-jar "%1")")
                .arg(QDir::toNativeSeparators(target.absoluteFilePath())),
            FileExecutionTypes::Executable};
  }

  if (isExecutable(target)) {
    return {target, "", FileExecutionTypes::Executable};
  }

  return {{}, {}, FileExecutionTypes::Other};
}

}  // namespace spawn

namespace helper
{
bool helperExec(QWidget* parent, const QString& moDirectory, const QString& commandLine,
                bool async)
{
  const QString fileName = QDir(moDirectory).path() % u"/helper"_s;

  const pid_t pid = fork();

  // error
  if (pid == -1) {
    const int e = errno;
    spawn::dialogs::helperFailed(parent, e, u"fork()"_s, fileName, moDirectory,
                                 commandLine);
    return false;
  }

  // child
  if (pid == 0) {
    // create argument list for execv
    vector<const char*> args = {fileName.toLocal8Bit()};
    QStringList spArgs       = QProcess::splitCommand(commandLine);
    for (const auto& arg : spArgs) {
      args.push_back(arg.toLocal8Bit());
    }
    // array must be terminated by a null pointer
    args.push_back(nullptr);

    chdir(QDir(moDirectory).absolutePath().toLocal8Bit());

    execv(args[0], const_cast<char* const*>(args.data()));

    // exec only returns on error
    const int e = errno;
    spawn::dialogs::helperFailed(parent, e, u"execv()"_s, fileName, moDirectory,
                                 commandLine);
    return false;
  }

  // parent
  if (async) {
    return true;
  }

  int wstatus = 0;
  if (waitpid(pid, &wstatus, 0) == -1) {
    const int e = errno;
    spawn::dialogs::helperFailed(parent, e, u"waitpid()"_s, fileName, moDirectory,
                                 commandLine);
    return false;
  }

  if (WIFEXITED(wstatus)) {
    return WEXITSTATUS(wstatus) == 0;
  }

  spawn::dialogs::helperFailed(parent, ECANCELED,
                               u"Process did not terminate normally"_s, fileName,
                               moDirectory, commandLine);

  return false;
}

bool adminLaunch(QWidget* parent, const std::filesystem::path& moPath,
                 const std::filesystem::path& moFile,
                 const std::filesystem::path& workingDir)
{
  STUB();
  // TODO: should this be implemented?
  //  could be done using KDESu
  return false;
}
}  // namespace helper
