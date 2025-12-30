#include "spawn.h"

#include "env.h"
#include "envos.h"
#include "envsecurity.h"
#include "settings.h"
#include "shared/util.h"
#include "stub.h"
#include <QDirIterator>
#include <QMessageBox>
#include <QProcess>
#include <QString>
#include <log.h>
#include <report.h>
#include <steamutility.h>
#include <sys/wait.h>
#include <usvfs-fuse/usvfsmanager.h>
#include <utility.h>

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

// custom error codes
static constexpr int PROTON_NOT_FOUND      = 200;
static constexpr int COMPAT_DATA_NOT_FOUND = 201;
static constexpr int STEAM_NOT_FOUND       = 202;
static constexpr int APPID_EMPTY           = 203;
static constexpr int MOUNT_ERROR           = 204;
static constexpr int UNKNOWN_ERROR         = 300;

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
  if (auto b = env::Environment().getOSInfo().isElevated()) {
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

QString makeContent(const SpawnParameters& sp, DWORD code)
{
  switch (code) {
  case PROTON_NOT_FOUND:
    return u"could not find proton executable"_s;
  case COMPAT_DATA_NOT_FOUND:
    return u"could not find compat data directory"_s;
  case STEAM_NOT_FOUND:
    return u"could not find steam installation path"_s;
  case APPID_EMPTY:
    return u"appid is empty"_s;
  case MOUNT_ERROR:
    return u"mount error"_s;
  default:
    return {strerror(static_cast<int>(code))};
  }
}

QMessageBox::StandardButton badSteamPath(QWidget* parent)
{
  const auto details = QStringLiteral(
      "can't start steam because it was not found. Tried PATH and flatpak");

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

QMessageBox::StandardButton startSteamFailed(QWidget* parent, const QString& location,
                                             const QString& error, int e)
{
  auto details = QString("a steam install was found in %1").arg(location);

  SpawnParameters sp;
  sp.binary = QFileInfo(location);
  details += makeDetails(sp, e, error);

  log::error("{}", details);

  return MOBase::TaskDialog(parent, QObject::tr("Cannot start Steam"))
      .main(QObject::tr("Cannot start Steam"))
      .content(makeContent(sp, e))
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
  (void)parent;
  (void)sp;
  return false;
}

}  // namespace spawn::dialogs

namespace spawn
{

// located in spawn.cpp
extern bool isExeFile(const QFileInfo& target);
extern bool isJavaFile(const QFileInfo& target);
extern QString makeSteamArguments(const QString& username, const QString& password);

void logSpawning(const SpawnParameters& sp, const QString& realCmd)
{
  log::debug("spawning binary:\n"
             " . exe: '{}'\n"
             " . args: '{}'\n"
             " . cwd: '{}'\n"
             " . steam id: '{}'\n"
             " . hooked: {}\n"
             " . stdout: {}\n"
             " . stderr: {}\n"
             " . real cmd: '{}'",
             sp.binary.absoluteFilePath(), sp.arguments,
             sp.currentDirectory.absolutePath(), sp.steamAppID, sp.hooked,
             (sp.stdOut == INVALID_HANDLE_VALUE ? "no" : "yes"),
             (sp.stdErr == INVALID_HANDLE_VALUE ? "no" : "yes"), realCmd);
}

DWORD spawn(const SpawnParameters& sp, HANDLE& processHandle)
{
  if (sp.hooked) {
    pid_t pid = UsvfsManager::instance()->usvfsCreateProcessHooked(
        sp.binary.absoluteFilePath().toStdString(), sp.arguments.toStdString(),
        sp.binary.absolutePath().toStdString());
    if (pid >= 0) {
      processHandle = pidfd_open(pid, 0);
      return 0;
    }
    // todo: add proper error codes
    errno = UNKNOWN_ERROR;
  } else {
    auto result = shell::ExecuteIn(sp.binary.absoluteFilePath(),
                                   sp.binary.absolutePath(), sp.arguments);
    if (result.success()) {
      processHandle = result.stealProcessHandle();
      return 0;
    }
  }

  const int e = errno;
  log::error("error running {}, {}", sp.binary.absoluteFilePath().toStdString(),
             strerror(e));
  return e;
}

int spawnProton(const SpawnParameters& sp, HANDLE& pidFd)
{
  // check the steam path first to fail early if it is not found
  QString steamPath = findSteamCached();
  if (steamPath.isEmpty()) {
    return STEAM_NOT_FOUND;
  }

  // appID is required to get the proton version to use as well as the compatdata path
  if (sp.steamAppID.isEmpty()) {
    return APPID_EMPTY;
  }

  // command is
  // STEAM_COMPAT_DATA_PATH=compatdata/<appid>
  // STEAM_COMPAT_CLIENT_INSTALL_PATH=<steam path>
  // SteamGameId=<appid>
  // path/to/proton run application.exe

  // the application is located at steamapps/common/<appliation>
  // compatdata is located at steamapps/compatdata/<appid>

  if (sp.prefixDirectory.isEmpty()) {
    log::error("prefixDirectory is empty");
    return COMPAT_DATA_NOT_FOUND;
  }
  log::debug("Using compatdata dir {}", sp.prefixDirectory);

  QString proton = findProtonByAppID(sp.steamAppID);
  if (proton.isEmpty()) {
    return PROTON_NOT_FOUND;
  }

  const QString params =
      "run \""_L1 % sp.binary.absoluteFilePath() % "\" "_L1 % sp.arguments;

  if (sp.hooked) {
    vector<string> env;
    for (int i = 0; environ[i] != nullptr; ++i) {
      env.push_back(environ[i]);
    }

    env.push_back("STEAM_COMPAT_DATA_PATH=" + sp.prefixDirectory.toStdString());
    env.push_back("STEAM_COMPAT_CLIENT_INSTALL_PATH=" + steamPath.toStdString());
    env.push_back("SteamGameId=" + sp.steamAppID.toStdString());

    pid_t pid = UsvfsManager::instance()->usvfsCreateProcessHooked(
        proton.toStdString(), params.toStdString(),
        sp.binary.absolutePath().toStdString(), std::move(env));

    if (pid >= 0) {
      pidFd = pidfd_open(pid, 0);
      return 0;
    }
    // todo: add proper error codes
    errno = UNKNOWN_ERROR;
  } else {
    // todo: create function to pass env to
    setenv("STEAM_COMPAT_DATA_PATH", sp.prefixDirectory.toLocal8Bit(), 1);
    setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamPath.toLocal8Bit(), 1);
    setenv("SteamGameId", sp.steamAppID.toLocal8Bit(), 1);

    auto result = shell::ExecuteIn(proton, sp.binary.absolutePath(), params);

    if (result.success()) {
      pidFd = result.stealProcessHandle();
      return 0;
    }
  }

  const int e = errno;
  log::error("error running {}, {}", sp.binary.absoluteFilePath().toStdString(),
             strerror(e));
  return e;
}

bool restartAsAdmin(QWidget* parent)
{
  STUB();
  (void)parent;
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

  // todo: check if steam should really be detached
  QProcess p;
  p.setProgram(binary);
  p.setArguments(QProcess::splitCommand(arguments));
  if (!p.startDetached()) {
    const auto r =
        dialogs::startSteamFailed(parent, binary, p.errorString(), p.error());

    return (r == QMessageBox::Yes);
  }

  QMessageBox::information(
      parent, QObject::tr("Waiting"),
      QObject::tr("Please press OK once you're logged into steam."));

  return true;
}

std::optional<QString> checkSteamFiles(const QDir& dir)
{
  static const QStringList steamFiles = {u"steam_api.dll"_s, u"steam_api64.dll"_s};

  // check windows files
  for (const auto& file : steamFiles) {
    const QString filepath = dir.absoluteFilePath(file);
    if (QFileInfo::exists(filepath)) {
      return filepath;
    }
  }

  // check linux files
  // the library can be in an arbitrary location, so a recursive search may be required
  // examples:
  // - unity games: <gamename>_Data/Plugins/libsteam_api.so
  //                <gamename>_Data/Plugins/x86_64/libsteam_api.so
  // - baldurs gate 3: bin/libsteam_api.so
  // - factorio, X4: lib/libsteam_api.so
  // - bastion: lib64/libsteam_api.so
  // - bitburner: resources/app/lib/libsteam_api.so
  // - war thunder: linux64/libsteam_api.so

  // best case performance (file in .): 990 ns
  // worst case performance (file non-existing): 36,247 ns

  // list of known paths
  static const QStringList steamFilesLinux = {
      u"libsteam_api.so"_s,         u"bin/libsteam_api.so"_s,
      u"lib/libsteam_api.so"_s,     u"lib64/libsteam_api.so"_s,
      u"linux64/libsteam_api.so"_s, u"resources/app/lib/libsteam_api.so"_s,
  };
  static const QStringList steamFilesLinuxUnity = {
      u"Plugins/libsteam_api.so"_s,
      u"Plugins/x86_64/libsteam_api.so"_s,
  };

  const QString absDir = dir.absolutePath();

  // try some generic paths
  for (const auto& file : steamFilesLinux) {
    QString filePath = absDir % "/"_L1 % file;
    if (QFileInfo::exists(filePath)) {
      return filePath;
    }
  }

  // try unity-specific paths
  QDirIterator it(dir);
  while (it.hasNext()) {
    auto entry = it.next();
    if (entry.endsWith("_Data"_L1)) {
      for (const auto& file : steamFilesLinuxUnity) {
        const QString filePath = entry % "/"_L1 % file;
        if (QFileInfo::exists(filePath)) {
          return filePath;
        }
      }
    }
  }

  // try recursive search
  // entryList is much faster than both QDirListing and QDirIterator
  // entryList: 18,452 ns
  // QDirListing: 437,715 ns
  // QDirIterator: 631,422 ns
  auto list = dir.entryList(QStringList{u"libsteam_api.so"_s}, QDir::Files);
  if (!list.isEmpty()) {
    return list.first();
  }

  return {};
}

HANDLE startBinary(QWidget* parent, const SpawnParameters& sp)
{
  HANDLE handle = INVALID_HANDLE_VALUE;
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
    if (sp.hooked) {
      UsvfsManager::instance()->unmount();
    }
    return INVALID_HANDLE_VALUE;
  }

  default: {
    if (sp.hooked) {
      UsvfsManager::instance()->unmount();
    }
    dialogs::spawnFailed(parent, sp, e);
    return INVALID_HANDLE_VALUE;
  }
  }
}

QString findJavaInstallation(const QString&)
{
  // try PATH
  QString java = QStandardPaths::findExecutable(u"java"_s);
  if (!java.isEmpty()) {
    return java;
  }

  // try JAVA_HOME
  char* javaHome = getenv("JAVA_HOME");
  if (javaHome != nullptr) {
    QString tmp = QString::fromUtf8(javaHome);
    if (!tmp.endsWith('/')) {
      tmp += '/';
    }
    return tmp % u"bin/java"_s;
  }

  // not found
  return {};
}

FileExecutionContext getFileExecutionContext(QWidget*, const QFileInfo& target)
{
  if (isJavaFile(target)) {
    return {QFileInfo(u"java"_s),
            QStringLiteral(R"(-jar "%1")")
                .arg(QDir::toNativeSeparators(target.absoluteFilePath())),
            FileExecutionTypes::Executable};
  }

  if (isExeFile(target)) {
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
  const QString fileName = QDir(moDirectory).path() % "/helper"_L1;

  shell::Result result = shell::ExecuteIn(fileName, commandLine, moDirectory);

  if (!result.success()) {
    const int e = errno;
    spawn::dialogs::helperFailed(parent, e, u"Execute()"_s, fileName, moDirectory,
                                 commandLine);
    return false;
  }

  if (async) {
    return true;
  }

  int pidFd = result.processHandle();
  siginfo_t info{};

  if (waitid(P_PIDFD, pidFd, &info, WEXITED | WSTOPPED) == -1) {
    const int e = errno;
    spawn::dialogs::helperFailed(parent, e, u"waitpid()"_s, fileName, moDirectory,
                                 commandLine);
    return false;
  }

  if (info.si_code == CLD_EXITED) {
    return info.si_status == 0;
  }

  spawn::dialogs::helperFailed(parent, ECANCELED, u"Process did not exit normally"_s,
                               fileName, moDirectory, commandLine);
  return false;
}

}  // namespace helper
