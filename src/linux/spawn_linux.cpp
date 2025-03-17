#include "env.h"
#include "envos.h"
#include "envsecurity.h"
#include "overlayfs/OverlayfsManager.h"
#include "settings.h"
#include "shared/util.h"
#include "spawn.h"
#include "stub.h"
#include "vdf_parser.hpp"
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

struct steamGame
{
  string name;
  filesystem::path installDir;
  string appID;
};

struct steamLibrary
{
  filesystem::path path;
  vector<steamGame> games;
};

// this function may save >10µs on subsequent calls
QString findSteamCached() noexcept
{
  static const QString steam = findSteam();
  return steam;
}

vector<steamLibrary> getAllSteamLibraries() noexcept
{
  TimeThis tt(u"getAllSteamLibraries()"_s);

  QString steamPath = findSteamCached();
  if (steamPath.isEmpty()) {
    log::error("could not find steam installation path");
    return {};
  }
  filesystem::path steamConfigPath = steamPath.append("/config"_L1).toStdString();

  vector<steamLibrary> libraries;

  ifstream libraryFoldersFile(steamConfigPath / "libraryfolders.vdf");
  if (!libraryFoldersFile.is_open()) {
    log::error("error opening libraryfolders.vdf");
    return {};
  }
  auto root = tyti::vdf::read(libraryFoldersFile);

  for (const auto& library : root.childs) {
    // skip empty libraries
    if (library.second->childs["apps"] == nullptr) {
      continue;
    }

    steamLibrary tmp;
    tmp.path = library.second->attribs["path"];

    for (const auto& app : library.second->childs["apps"]->attribs) {
      steamGame game;
      game.appID = app.first;
      // parse manifest
      filesystem::path manifest = tmp.path / "steamapps/appmanifest_";
      manifest += game.appID + ".acf";
      try {
        ifstream manifestFile(manifest);
        if (!manifestFile.is_open()) {
          // steam may not have cleaned up
          const int e = errno;
          log::warn("error opening manifest file {}, {}", manifest.generic_string(),
                    strerror(e));
          continue;
        }
        auto appState = tyti::vdf::read(manifestFile);

        game.name       = appState.attribs.at("name");
        game.installDir = appState.attribs.at("installdir");
      } catch (const out_of_range& ex) {
        log::error("out_of_range exception while parsing manifest file {}: {}",
                   manifest.generic_string(), ex.what());
        return {};
      }
      tmp.games.push_back(game);
    }
    libraries.push_back(tmp);
  }
  return libraries;
}

/**
 * @brief Builds an index of all steam libraries and stores it for faster
 * subsequent
 * calls.
 * This may save a few milliseconds per call, possibly more if
 * mechanical
 * drives are involved.
 */
vector<steamLibrary> getAllSteamLibrariesCached() noexcept
{
  static const vector<steamLibrary> libraries = getAllSteamLibraries();
  return libraries;
}

vector<steamGame> getAllSteamApps()
{
  vector<steamGame> games;
  filesystem::path steamConfigPath =
      findSteamCached().append("/config"_L1).toStdString();

  vector<steamLibrary> libraries = getAllSteamLibrariesCached();
  for (auto& library : libraries) {
    games.reserve(games.size() + library.games.size());
    for (const auto& game : library.games) {
      games.emplace_back(game);
    }
  }
  return games;
}

/**
 * @brief Gets the proton name that is configured for the specified appID.
 * @param
 * appID Steam appID of application
 * @return Proton name
 */
string getProtonNameByAppID(const QString& appID) noexcept
{
  // proton versions can be parsed from ~/.local/Steam/config/config.vdf
  // InstallConfigStore -> Software -> Valve -> Steam -> CompatToolMapping
  try {
    QString steamPath = findSteamCached();
    if (steamPath.isEmpty()) {
      log::error("could not find steam installation path");
      return {};
    }

    // todo: add proper support for flatpak installations, config file path may be
    // different
    string configPath = steamPath.append(u"/config/config.vdf"_s).toStdString();

    log::debug("parsing {}", configPath);

    const string message = format("error parsing {}", configPath);
    ifstream config(configPath);
    if (!config.is_open()) {
      const int error = errno;
      log::error("could not open steam config file {}: {}", configPath,
                 strerror(error));
      return {};
    }
    auto root     = tyti::vdf::read(config);
    auto software = root.childs.at("Software");
    // according to ProtonUp-Qt source code the key can either be "Valve" or "valve"
    auto valve = software->childs["Valve"];
    if (valve == nullptr) {
      // "Valve" does not exist, try "valve"
      valve = software->childs.at("valve");
    }
    auto compatToolMapping = valve->childs.at("Steam")->childs.at("CompatToolMapping");
    auto tmp               = compatToolMapping->childs[appID.toStdString()];
    if (tmp != nullptr) {
      return tmp->attribs["name"];
    }
    // version not set manually, use global version
    return compatToolMapping->childs.at("0")->attribs.at("name");
  } catch (const out_of_range& ex) {
    log::error("error getting proton name for appid {}, {}", appID, ex.what());
    return {};
  }
}

/**
 * @brief Gets proton executable for specified appID
 * @param appID Steam appID of
 * application
 * @return Absolute path of proton executable, empty string on error
 */
QString findProtonByAppID(const QString& appID) noexcept
{
  QString steamPath = findSteamCached();
  if (steamPath.isEmpty()) {
    log::error("could not find steam installation path");
    return {};
  }

  string protonName = getProtonNameByAppID(appID);
  if (protonName.empty()) {
    return {};
  }

  log::debug("got proton name {}", protonName);

  QString proton;
  if (protonName.starts_with("proton_")) {
    // normal proton, installed as steam application

    // convert "proton_<version>" to "Proton <version>"
    protonName.replace(0, strlen("proton_"), "Proton ");

    bool found = false;
    for (const auto& library : getAllSteamLibrariesCached()) {
      for (const auto& game : library.games) {
        if (game.name.starts_with(protonName)) {
          QString location = QString::fromStdString(
              (library.path / "steamapps/common" / game.installDir / "proton")
                  .string());
          if (QFile::exists(location)) {
            log::debug("found proton at {}", location);
            proton = location;
            found  = true;
            break;
          }
          log::warn("found proton in config, but file {} does not exist", location);
        }
      }
      if (found) {
        break;
      }
    }
  } else {
    // custom proton e.g. GE-Proton9-25, installed in steam/compatibilitytools.d/
    proton = steamPath % u"/compatibilitytools.d/"_s %
             QString::fromStdString(protonName) % u"/proton"_s;
    if (!QFile::exists(proton)) {
      log::error("detected proton path \"{}\" does not exist", proton);
      return {};
    }
    log::debug("proton found at {}", proton);
  }
  return proton;
}

/**
 * @brief Returns path of compat data directory for the specified appID
 * @param
 * appID Steam appID of application
 * @return Absolute path to compat data, empty
 * string if not found
 */
QString findCompatDataByAppID(const QString& appID) noexcept
{
  QString steamPath = findSteamCached();
  if (steamPath.isEmpty()) {
    log::error("could not find steam installation path");
    return {};
  }

  for (const auto& library : getAllSteamLibrariesCached()) {
    for (const auto& game : library.games) {
      if (appID == game.appID) {
        filesystem::path compatData = library.path / "steamapps/common" /
                                      game.installDir / "../../compatdata" /
                                      appID.toStdString();
        compatData = canonical(compatData);
        log::debug("found compatdata for appID {}: {}", appID, compatData.string());
        return QString::fromStdString(absolute(compatData).string());
      }
    }
  }
  return {};
}

/**
 * @brief Gets the appID of the game located in the specified location by parsing
 * ../../appmanifest_*.acf
 * @param gameLocation Location of game
 * @return Steam
 * appID of game in specified location
 */
QString appIdByGamePath(const QString& gameLocation) noexcept
{
  log::debug("looking up appID for game path {}", gameLocation);
  // get steamApps directory for the library the game is located in
  QString steamAppsPath    = gameLocation;
  qsizetype commonPosition = steamAppsPath.indexOf(u"common"_s, Qt::CaseInsensitive);
  // remove everything from "common" to end
  steamAppsPath.truncate(commonPosition);

  // get game installation path in steamapps/common/
  QString installPath = gameLocation;
  // remove everything from beginning to common/
  installPath.remove(0, commonPosition + QStringLiteral("common/").size());
  // look for the first slash in case gameLocation is a subdirectory
  auto pos = installPath.indexOf('/');
  if (pos != -1) {
    installPath.truncate(pos);
  }

  // iterate over app manifests
  QDirIterator it(steamAppsPath, QStringList(u"appmanifest_*.acf"_s), QDir::Files);
  while (it.hasNext()) {
    QString item = it.next();

    ifstream file(item.toStdString());
    auto root         = tyti::vdf::read(file);
    string installdir = root.attribs["installdir"];
    if (installdir.empty()) {
      log::error("error parsing appmanifest: installdir not found");
      return {};
    }
    if (installPath == installdir) {
      QString appID = QString::fromStdString(root.attribs["appid"]);
      log::debug("found appid {}", appID);
      return appID;
    }
  }

  log::error("error getting appid for path {}", gameLocation);
  return {};
}

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
    if (!OverlayfsManager::getInstance().mount()) {
      return -1;
    }
  }

  // create argument list for execv
  vector<const char*> args = {sp.binary.absoluteFilePath().toLocal8Bit()};
  QStringList spArgs       = QProcess::splitCommand(sp.arguments);
  for (const auto& arg : spArgs) {
    args.push_back(arg.toLocal8Bit());
  }
  // array must be terminated by a null pointer
  args.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0) {
    // child
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
    processHandle = result;
    return 0;
  }

  const int e = errno;
  log::error("error running {}, {}", sp.binary.absoluteFilePath().toStdString(),
             strerror(e));
  return e;
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
    if (!OverlayfsManager::getInstance().mount()) {
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
  if (sp.binary.suffix() == "exe") {
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

bool isExeFile(const QFileInfo& target)
{
  return target.isExecutable() && target.isFile();
}

QFileInfo getCmdPath()
{
  return QFileInfo(u"/bin/sh"_s);
}

extern bool isJavaFile(const QFileInfo& target);

FileExecutionContext getFileExecutionContext(QWidget* parent, const QFileInfo& target)
{
  if (isExeFile(target)) {
    return {target, "", FileExecutionTypes::Executable};
  }

  if (isBatchFile(target)) {
    return {getCmdPath(),
            QStringLiteral(R"(-c "%1")")
                .arg(QDir::toNativeSeparators(target.absoluteFilePath())),
            FileExecutionTypes::Executable};
  }

  if (isJavaFile(target)) {
    auto java = findJavaInstallation(target.absoluteFilePath());

    if (java.isEmpty()) {
      java = QFileDialog::getOpenFileName(parent, QObject::tr("Select executable"),
                                          QString());
    }

    if (!java.isEmpty()) {
      return {QFileInfo(java),
              QStringLiteral(R"(-jar "%1")")
                  .arg(QDir::toNativeSeparators(target.absoluteFilePath())),
              FileExecutionTypes::Executable};
    }
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

  QProcess process;

  process.setWorkingDirectory(moDirectory);
  process.startCommand(fileName % u" "_s % commandLine);

  if (!process.waitForStarted(1000)) {
    spawn::dialogs::helperFailed(parent, process.error(), process.errorString(),
                                 fileName, moDirectory, commandLine);

    return false;
  }

  if (async) {
    return true;
  }

  if (!process.waitForFinished()) {
    spawn::dialogs::helperFailed(parent, process.error(), process.errorString(),
                                 fileName, moDirectory, commandLine);

    return false;
  }

  int exitCode = process.exitCode();
  if (exitCode != 0) {
    spawn::dialogs::helperFailed(parent, process.error(), process.errorString(),
                                 fileName, moDirectory, commandLine);

    return false;
  }

  return true;
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
