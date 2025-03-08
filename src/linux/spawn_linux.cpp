#include "env.h"
#include "envos.h"
#include "envsecurity.h"
#include "overlayfs/OverlayfsManager.h"
#include "settings.h"
#include "shared/util.h"
#include "spawn.h"
#include "stub.h"
#include <QMessageBox>
#include <QString>
#include <uibase/log.h>
#include <uibase/report.h>
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

  int user = 0;
  int group = 0;
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
  const auto details =
      QStringLiteral("can't start steam because it was not found. tried "
                     "/usr/bin/steam, steam.desktop, and flatpak");

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

  QProcess p;
  p.setProgram(QDir::toNativeSeparators(sp.binary.absoluteFilePath()));
  p.setArguments(QProcess::splitCommand(sp.arguments));
  p.setWorkingDirectory(QDir::toNativeSeparators(sp.currentDirectory.absolutePath()));

  qint64 pid;
  if (p.startDetached(&pid)) {
    processHandle = pidfd_open(pid, 0);
    if (processHandle == -1) {
      return errno;
    }
  }

  return EXIT_SUCCESS;
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
  // try path
  QString steam = QStandardPaths::findExecutable(u"steam"_s);
  if (!steam.isEmpty()) {
    return {steam, 0};
  }

  // try flatpak
  GError* e = nullptr;
  FlatpakInstallation* installation = flatpak_installation_new_user(nullptr, &e);
  if (e != nullptr) {
    g_error_free(e);
    return {{}, dialogs::badSteamPath(parent)};
  }

  FlatpakInstalledRef* flatpakInstalledRef = flatpak_installation_get_installed_ref(
      installation, FLATPAK_REF_KIND_APP, "com.valvesoftware.Steam", nullptr, "stable",
      nullptr, &e);
  if (e != nullptr || flatpakInstalledRef == nullptr) {
    if (e != nullptr) {
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
    GError* e;
    FlatpakInstallation* installation = flatpak_installation_new_user(nullptr, &e);
    if (e != nullptr) {
      g_error_free(e);
      return false;
    }

    if (flatpak_installation_launch(installation, steamFlatpak.toStdString().c_str(),
                                    nullptr, "stable", nullptr, nullptr, &e)) {
      return true;
    }
    if (e) {
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
  const auto e  = spawn::spawn(sp, handle);

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
