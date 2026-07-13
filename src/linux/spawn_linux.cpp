#include "spawn.h"

#include "env.h"
#include "envos.h"
#include "envsecurity.h"
#include "settings.h"
#include "shared/util.h"
#include <QDirIterator>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <report.h>
#include <steamutility.h>
#include <sys/wait.h>
#include <usvfs-fuse/usvfsmanager.h>
#include <utility.h>

using namespace MOBase;
using namespace MOShared;
using namespace std;
using namespace Qt::StringLiterals;

// custom error codes
static constexpr int PROTON_NOT_FOUND      = 200;
static constexpr int COMPAT_DATA_NOT_FOUND = 201;
static constexpr int STEAM_NOT_FOUND       = 202;
static constexpr int APPID_EMPTY           = 203;
static constexpr int MOUNT_ERROR           = 204;
static constexpr int RUNTIME_NOT_FOUND     = 205;
static constexpr int UNKNOWN_ERROR         = 300;

namespace
{
const QRegularExpression requiredToolRegex(uR"-("required_tool_appid" "(\d+)")-"_s);
const QRegularExpression toolCommandlineRegex(uR"-("commandline" "(\d+)")-"_s);
const QRegularExpression libraryRegex(uR"-(^\t"\d+"\n\t\{\n([\s\S]*?)\n\t\}$)-"_s,
                                      QRegularExpression::MultilineOption);
const QRegularExpression
    appsRegex(uR"-(^\t{2}"apps"\n\t{2}\{\n([\s\S]*?)\n\t{2}\}$)-"_s,
              QRegularExpression::MultilineOption);
const QRegularExpression pathRegex(uR"-(^\t*"path"\t*"(.*)"$)-"_s,
                                   QRegularExpression::MultilineOption);
const QRegularExpression installDirRegex(uR"-(^\t"installdir"\t*"(.*)"$)-"_s);

struct Tool
{
  QString requiredToolAppID;
  QString commandline;
};

Tool parseToolManifest(const QString& toolPath) noexcept(false)
{
  QString toolManifestPath = toolPath % "/toolmanifest.vdf"_L1;
  QFile toolManifestFile(toolManifestPath);
  if (!toolManifestFile.open(QIODevice::ReadOnly)) {
    throw runtime_error(format("Error opening '{}', {}", toolManifestPath,
                               toolManifestFile.errorString()));
  }

  Tool tool;

  QString data           = toolManifestFile.readAll();
  tool.commandline       = toolCommandlineRegex.match(data).captured();
  tool.requiredToolAppID = requiredToolRegex.match(data).captured();

  return tool;
}

QString getPathFromAppID(const QString& appID) noexcept(false)
{
  QString steamPath = findSteamCached();

  QString libraryFoldersPath = steamPath % "/config/libraryfolders.vdf"_L1;
  QFile libraryFoldersFile(libraryFoldersPath);
  if (!libraryFoldersFile.open(QIODevice::ReadOnly)) {
    throw runtime_error(format("Error opening '{}', {}", libraryFoldersPath,
                               libraryFoldersFile.errorString()));
  }
  QString libraryFoldersData = libraryFoldersFile.readAll();
  auto it                    = libraryRegex.globalMatch(libraryFoldersData);
  // iterate over library entries
  while (it.hasNext()) {
    auto match       = it.next();
    QString captured = match.captured();
    QString apps     = appsRegex.match(captured).captured(1);

    if (apps.contains('"' % appID % '"')) {
      // current library contains the appid we are looking for
      QString libPath = pathRegex.match(captured).captured(1);
      // parse appmanifest_<APPID>.acf
      QString appManifestPath =
          libPath % "/steamapps/appmanifest_"_L1 % appID % ".acf"_L1;
      QFile appManifest(appManifestPath);
      if (!appManifest.open(QIODevice::ReadOnly)) {
        throw runtime_error(format("Error opening '{}', {}", appManifestPath,
                                   appManifest.errorString()));
      }

      QString appManifestData = appManifest.readAll();
      QString installDir      = installDirRegex.match(appManifestData).captured(1);
      if (installDir.isEmpty()) {
        throw runtime_error("Error parsing " + appManifestPath.toStdString());
      }

      return libPath % "steamapps/common/"_L1 % installDir;
    }
  }

  throw runtime_error("Error finding a location for appID " + appID.toStdString());
}

QStringList createToolCommand(const QString& toolPath)
{
  const QString verb = u"waitforexitandrun"_s;
  Tool tool          = parseToolManifest(toolPath);
  tool.commandline.replace(u"%verb%"_s, verb);
  QStringList toolCommandline = {toolPath % tool.commandline};
  // if `require_tool_appid` is set, the tool needs to be wrapped in another tool,
  // which in turn could require to be wrapped in another tool
  while (!tool.requiredToolAppID.isEmpty()) {
    QString requiredToolPath = getPathFromAppID(tool.requiredToolAppID);
    tool                     = parseToolManifest(toolPath);

    tool.commandline.replace(u"%verb%"_s, verb);
    toolCommandline = QProcess::splitCommand(tool.commandline) << toolCommandline;
    toolCommandline.emplaceFront(requiredToolPath);
  }

  return toolCommandline;
}

}  // namespace

namespace spawn::dialogs
{

extern void spawnFailed(QWidget* parent, const SpawnParameters& sp, DWORD code);
extern void helperFailed(QWidget* parent, DWORD code, const QString& why,
                         const QString& binary, const QString& cwd,
                         const QString& args);

QString makeRightsDetails(const QFileInfo& info)
{
  auto permissions = info.permissions();

  int user   = 0;
  int group  = 0;
  int others = 0;

  if (permissions.testFlag(QFileDevice::ReadOwner)) {
    user += 4;
  }
  if (permissions.testFlag(QFileDevice::WriteOwner)) {
    user += 2;
  }
  if (permissions.testFlag(QFileDevice::ExeOwner)) {
    user += 1;
  }

  if (permissions.testFlag(QFileDevice::ReadGroup)) {
    group += 4;
  }
  if (permissions.testFlag(QFileDevice::WriteGroup)) {
    group += 2;
  }
  if (permissions.testFlag(QFileDevice::ExeGroup)) {
    group += 1;
  }

  if (permissions.testFlag(QFileDevice::ReadOther)) {
    others += 4;
  }
  if (permissions.testFlag(QFileDevice::WriteOther)) {
    others += 2;
  }
  if (permissions.testFlag(QFileDevice::ExeOther)) {
    others += 1;
  }

  return (permissions.testFlag(QFileDevice::ReadOwner) ? QChar('r') : QChar('-')) %
         (permissions.testFlag(QFileDevice::WriteOwner) ? QChar('w') : QChar('-')) %
         (permissions.testFlag(QFileDevice::ExeOwner) ? QChar('x') : QChar('-')) %
         (permissions.testFlag(QFileDevice::ReadGroup) ? QChar('r') : QChar('-')) %
         (permissions.testFlag(QFileDevice::WriteGroup) ? QChar('w') : QChar('-')) %
         (permissions.testFlag(QFileDevice::ExeGroup) ? QChar('x') : QChar('-')) %
         (permissions.testFlag(QFileDevice::ReadOther) ? QChar('r') : QChar('-')) %
         (permissions.testFlag(QFileDevice::WriteOther) ? QChar('w') : QChar('-')) %
         (permissions.testFlag(QFileDevice::ExeOther) ? QChar('x') : QChar('-')) %
         QStringLiteral(" (%1%2%3)").arg(user, group, others);
}

QString makeDetails(const SpawnParameters& sp, DWORD code, const QString& more = {})
{
  QString owner, rights;

  if (sp.binary.isFile()) {
    auto fs = env::getFileSecurity(sp.binary.absoluteFilePath());

    if (fs.error.isEmpty()) {
      owner  = fs.owner;
      rights = makeRightsDetails(sp.binary);
    } else {
      owner  = fs.error;
      rights = fs.error;
    }
  } else {
    owner  = u"(file not found)"_s;
    rights = u"(file not found)"_s;
  }

  const bool cwdExists =
      (sp.currentDirectory.isEmpty() ? true : sp.currentDirectory.exists());

  QString elevated;
  if (auto b = env::Environment().getOSInfo().isElevated()) {
    elevated = (*b ? u"yes"_s : u"no"_s);
  } else {
    elevated = u"(not available)"_s;
  }

  return "Error "_L1 % QString::number(code) % ' ' %
         strerrorname_np(static_cast<int>(code)) %
         (more.isEmpty() ? more : ", "_L1 % more) % ": "_L1 %
         strerror(static_cast<int>(code)) %
         "\n"
         " . binary: '"_L1 %
         sp.binary.absoluteFilePath() %
         "'\n"
         " . owner: "_L1 %
         owner %
         "\n"
         " . rights: "_L1 %
         rights %
         "\n"
         " . arguments: "_L1 %
         sp.arguments %
         "\n"
         " . cwd: '"_L1 %
         sp.currentDirectory.absolutePath() % '\'' %
         (cwdExists ? u""_s : u" (not found)"_s) %
         "\n"
         " . hooked: "_L1 %
         (sp.hooked ? "yes"_L1 : "no"_L1) %
         "\n"
         " . MO elevated: "_L1 %
         elevated;
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
  case RUNTIME_NOT_FOUND:
    return u"steam linux runtime not found"_s;
  default:
    return {strerror(static_cast<int>(code))};
  }
}

QMessageBox::StandardButton badSteamPath(QWidget* parent)
{
  const auto details = QStringLiteral("can't start steam because it was not found.");

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
  SpawnParameters sp;
  sp.binary = QFileInfo(location);
  QString details =
      "a steam install was found in "_L1 % location % makeDetails(sp, e, error);

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

}  // namespace spawn::dialogs

namespace spawn
{

// located in spawn.cpp
extern bool isExeFile(const QFileInfo& target);
extern bool isJavaFile(const QFileInfo& target);

void logSpawning(const SpawnParameters& sp, const QString& realCmd)
{
  log::debug("spawning binary:\n"
             " . exe: '{}'\n"
             " . args: '{}'\n"
             " . cwd: '{}'\n"
             " . prefix path: '{}'\n"
             " . steam id: '{}'\n"
             " . hooked: {}\n"
             " . stdout: {}\n"
             " . stderr: {}\n"
             " . real cmd: '{}'",
             sp.binary.absoluteFilePath(), sp.arguments,
             sp.currentDirectory.absolutePath(), sp.prefixDirectory.absolutePath(),
             sp.steamAppID, sp.hooked,
             (sp.stdOut == INVALID_HANDLE_VALUE ? "no" : "yes"),
             (sp.stdErr == INVALID_HANDLE_VALUE ? "no" : "yes"), realCmd);
}

DWORD spawn(const SpawnParameters& sp, HANDLE& processHandle)
{
  logSpawning(sp, sp.binary.absoluteFilePath() % ' ' % sp.arguments);
  if (sp.hooked) {
    pid_t pid = UsvfsManager::instance()->usvfsCreateProcessHooked(
        sp.binary.absoluteFilePath().toStdString(), sp.arguments.toStdString(),
        sp.currentDirectory.absolutePath().toStdString());
    if (pid >= 0) {
      processHandle = pidfd_open(pid, 0);
      return 0;
    }
    // todo: add proper error codes
    errno = UNKNOWN_ERROR;
  } else {
    auto result = shell::ExecuteIn(sp.binary.absoluteFilePath(),
                                   sp.currentDirectory.absolutePath(), sp.arguments);
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

  if (sp.prefixDirectory.isEmpty()) {
    log::error("prefixDirectory is empty");
    return COMPAT_DATA_NOT_FOUND;
  }
  log::debug("Using compatdata dir {}", sp.prefixDirectory.absolutePath());

  QString proton = protonByPrefixPath(sp.prefixDirectory);
  if (proton.isEmpty()) {
    return PROTON_NOT_FOUND;
  }

  // create the tool command line
  QStringList toolCommandline;
  try {
    QString protonDir = proton.chopped(7);
    toolCommandline   = createToolCommand(protonDir);
  } catch (const runtime_error& ex) {
    log::error(ex.what());
    return RUNTIME_NOT_FOUND;
  }

  // command is based on the documentation of umu-launcher
  // https://github.com/Open-Wine-Components/umu-launcher#what-does-it-do

  QString reaper = steamPath % "/ubuntu12_32/reaper"_L1;
  QString params =
      QStringLiteral(
          R"(SteamLaunch AppId=%1 -- "%2/ubuntu12_32/steam-launch-wrapper" -- %3 "%4" %5)")
          .arg(sp.steamAppID, steamPath, toolCommandline.join(' '),
               sp.binary.absoluteFilePath(), sp.arguments);

  QStringList env;
  env << "STEAM_COMPAT_DATA_PATH="_L1 % sp.prefixDirectory.absolutePath();
  env << "STEAM_COMPAT_CLIENT_INSTALL_PATH="_L1 % steamPath;
  if (sp.enableSteamAPI) {
    env << "STEAM_COMPAT_APP_ID="_L1 % sp.steamAppID;
    env << "SteamGameId="_L1 % sp.steamAppID;
    env << "SteamAppId="_L1 % sp.steamAppID;
  }
  if (sp.enableSteamOverlay) {
    env << "LD_PRELOAD="_L1 % steamPath % "/ubuntu12_32/gameoverlayrenderer.so:"_L1 %
               steamPath % "/ubuntu12_64/gameoverlayrenderer.so"_L1;
  }
  const QString shaderPath = QDir::cleanPath(sp.prefixDirectory.absolutePath() %
                                             "/../../shadercache/"_L1 % sp.steamAppID);
  env << "STEAM_COMPAT_SHADER_PATH="_L1 % shaderPath;
  env << "STEAM_COMPAT_MEDIA_PATH="_L1 % shaderPath % "/fozmediav1"_L1;
  env << "STEAM_COMPAT_TRANSCODED_MEDIA_PATH="_L1 % shaderPath;
  env << "STEAM_FOSSILIZE_DUMP_PATH="_L1 % shaderPath %
             "/fozpipelinesv6/steamapprun_pipeline_cache"_L1;
  env << "DXVK_STATE_CACHE_PATH="_L1 % shaderPath % "/DXVK_state_cache"_L1;

  if (sp.hooked) {
    const string steamPathStr = steamPath.toStdString();

    logSpawning(sp, reaper % ' ' % params);

    pid_t pid = UsvfsManager::instance()->usvfsCreateProcessHooked(
        reaper, params, sp.currentDirectory.absolutePath(), env);

    if (pid >= 0) {
      pidFd = pidfd_open(pid, 0);
      return 0;
    }
    // todo: add proper error codes
    errno = UNKNOWN_ERROR;
  } else {
    auto result =
        shell::Execute(reaper, sp.currentDirectory.absolutePath(), params, env);

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

bool restartAsAdmin(QWidget*)
{
  // no-op
  return false;
}

QString getSteamDesktopFile(QWidget* parent)
{
  QStringList steam = QStandardPaths::locateAll(QStandardPaths::ApplicationsLocation,
                                                u"steam.desktop"_s);

  QStringList steamFlatpak = QStandardPaths::locateAll(
      QStandardPaths::ApplicationsLocation, u"com.valvesoftware.Steam.desktop"_s);

  int total = steam.size() + steamFlatpak.size();

  if (total == 0) {
    return {};
  }

  if (total == 1) {
    if (steam.isEmpty()) {
      return steamFlatpak.first();
    }
    return steam.first();
  }

  log::debug("found {} steam desktop files, prompting user to select one", total);

  // multiple desktop files have been found, let the user decide which one to use
  // one possible reason is that steam is installed via package manager and flatpak
  // simultaneously
  QStringList paths;
  for (const auto& path : steam) {
    paths << "Steam ("_L1 % path % ')';
  }
  for (const auto& path : steamFlatpak) {
    paths << "Steam Flatpak ("_L1 % path % ')';
  }

  QInputDialog dialog(parent);
  dialog.setWindowTitle(QObject::tr("Select Steam installation"));
  dialog.setLabelText(QObject::tr("Multiple Steam desktop files have been found, "
                                  "please select which one you'd like to use"));
  dialog.setOption(QInputDialog::UseListViewForComboBoxItems);
  dialog.setComboBoxItems(paths);

  int result = dialog.exec();
  if (result == QDialog::Rejected) {
    log::debug("user didn't select anything");
    return {};
  }

  QString selection = dialog.textValue();
  log::debug("user selection: {}", selection);
  selection.remove(0, selection.indexOf('(') + 1);
  selection.chop(1);

  return selection;
}

QStringList makeSteamArguments(const QString& username, const QString& password)
{
  QStringList args;

  if (!username.isEmpty()) {
    args << u"-login"_s << username;

    if (!password.isEmpty()) {
      args << password;
    }
  }

  return args;
}

bool startSteam(QWidget* parent)
{
  QString desktopFile = getSteamDesktopFile(parent);
  if (desktopFile.isEmpty()) {
    return dialogs::badSteamPath(parent) == QMessageBox::Yes;
  }

  // extract exec line
  QFile file(desktopFile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    log::error("error opening {}: {}", desktopFile, file.errorString());
    return false;
  }
  QTextStream in(&file);
  QString line;
  while (in.readLineInto(&line)) {
    if (!line.startsWith("Exec="_L1)) {
      continue;
    }
    line.remove("Exec="_L1);
    break;
  }
  if (line.isEmpty()) {
    log::error("error parsing desktop file {}", desktopFile);
    return false;
  }

  // clean up line
  static QRegularExpression regex(uR"(%[fFuUick])"_s);
  line.remove(regex);
  line = line.trimmed();

  QStringList arguments = QProcess::splitCommand(line);
  if (arguments.isEmpty()) {
    log::error("error parsing steam desktop file");
    return false;
  }

  // See if username and password supplied. If so, pass them into steam.
  QString username, password;
  if (Settings::instance().steam().login(username, password)) {
    if (username.length() > 0)
      MOBase::log::getDefault().addToBlacklist(username.toStdString(),
                                               "STEAM_USERNAME");
    if (password.length() > 0)
      MOBase::log::getDefault().addToBlacklist(password.toStdString(),
                                               "STEAM_PASSWORD");
    arguments.append(makeSteamArguments(username, password));
  }

  log::debug("starting steam process:\n"
             " . program: '{}'\n"
             " . username={}, password={}",
             desktopFile.toStdString(), (username.isEmpty() ? "no" : "yes"),
             (password.isEmpty() ? "no" : "yes"));

  QProcess p;
  p.setProgram(arguments.takeFirst());
  p.setArguments(arguments);
  if (!p.startDetached()) {
    const auto r =
        dialogs::startSteamFailed(parent, desktopFile, p.errorString(), p.error());

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
  // the library can be in an arbitrary location, so a recursive search may be required.
  // examples:
  // - unity games: <gamename>_Data/Plugins/libsteam_api.so
  //                <gamename>_Data/Plugins/x86_64/libsteam_api.so
  // - baldurs gate 3: bin/libsteam_api.so
  // - factorio, X4: lib/libsteam_api.so
  // - bastion: lib64/libsteam_api.so
  // - bitburner: resources/app/lib/libsteam_api.so
  // - war thunder: linux64/libsteam_api.so
  // - starbound: linux/libsteam_api.so

  // best case performance (file in .): 990 ns
  // worst case performance (file non-existing): 36,247 ns

  // list of known paths
  static const QStringList steamFilesLinux = {
      u"libsteam_api.so"_s,
      u"bin/libsteam_api.so"_s,
      u"lib/libsteam_api.so"_s,
      u"lib64/libsteam_api.so"_s,
      u"linux64/libsteam_api.so"_s,
      u"linux/libsteam_api.so"_s,
      u"resources/app/lib/libsteam_api.so"_s,
  };
  static const QStringList steamFilesLinuxUnity = {
      u"Plugins/libsteam_api.so"_s,
      u"Plugins/x86_64/libsteam_api.so"_s,
  };

  const QString absDir = dir.absolutePath();

  // try some generic paths
  for (const auto& file : steamFilesLinux) {
    QString filePath = absDir % '/' % file;
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
        const QString filePath = entry % '/' % file;
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
  if (sp.binary.suffix() == "desktop"_L1) {
    QString path = sp.binary.absoluteFilePath();

    // extract exec line
    // see
    // https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html
    // todo: test this more extensively
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      log::error("error opening {}: {}", path, file.errorString());
      return -1;
    }
    QTextStream in(&file);
    QString line;
    while (in.readLineInto(&line)) {
      if (!line.startsWith("Exec="_L1)) {
        continue;
      }
      line.remove("Exec="_L1);
      break;
    }
    if (line.isEmpty()) {
      log::error("error parsing desktop file {}", path);
      return -1;
    }

    static QRegularExpression regex(uR"(%[fFuUdDnNickvm])"_s);
    line.remove(regex);
    line = line.trimmed();

    auto firstSpace = line.indexOf(' ');

    SpawnParameters p = sp;
    p.binary          = QFileInfo(line.left(firstSpace));
    p.arguments       = line.remove(0, firstSpace).trimmed() % ' ' % sp.arguments;

    e = spawn(p, handle);
  } else if (sp.binary.suffix() == "exe"_L1) {
    e = spawnProton(sp, handle);
  } else {
    e = spawn(sp, handle);
  }

  switch (e) {
  case 0: {
    return handle;
  }

  case EACCES: {
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
  QString javaHome = qEnvironmentVariable("JAVA_HOME");
  if (!javaHome.isEmpty()) {
    if (!javaHome.endsWith('/')) {
      javaHome = javaHome % '/';
    }
    return javaHome % "bin/java"_L1;
  }

  // not found
  return {};
}

FileExecutionContext getFileExecutionContext(QWidget*, const QFileInfo& target)
{
  if (isJavaFile(target)) {
    QString java = findJavaInstallation(target.absoluteFilePath());
    if (!java.isEmpty()) {
      return {QFileInfo(java),
              QStringLiteral(R"(-jar "%1")").arg(target.absoluteFilePath()),
              FileExecutionTypes::Executable};
    }
  }

  else if (isExeFile(target)) {
    return {target, {}, FileExecutionTypes::Executable};
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
