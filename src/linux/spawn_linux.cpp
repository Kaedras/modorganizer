#include "spawn.h"

#include "shared/util.h"
#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QtDebug>
#include <uibase/errorcodes.h>
#include <uibase/log.h>
#include <uibase/report.h>
#include <uibase/utility.h>

#include "stub.h"

using namespace MOBase;
using namespace MOShared;
using namespace std;

namespace spawn::dialogs
{

void spawnFailed(QWidget* parent, const SpawnParameters& sp, DWORD code);

void helperFailed(QWidget* parent, const QString& error, const QString& why,
                  const QString& binary, const QString& cwd, const QString& args)
{
  STUB();
}

std::string makeRightsDetails(const QFileInfo& info)
{
  string s;

  auto permissions = info.permissions();

  s += "owner: ";
  if (permissions.testFlag(QFileDevice::ReadOwner)) {
    s += "r";
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::WriteOwner)) {
    s += "w";
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::ExeOwner)) {
    s += "x";
  } else {
    s += "-";
  }

  s += " group: ";
  if (permissions.testFlag(QFileDevice::ReadGroup)) {
    s += "r";
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::WriteGroup)) {
    s += "w";
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::ExeGroup)) {
    s += "x";
  } else {
    s += "-";
  }

  s += " others: ";
  if (permissions.testFlag(QFileDevice::ReadOther)) {
    s += "r";
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::WriteOther)) {
    s += "w";
  } else {
    s += "-";
  }
  if (permissions.testFlag(QFileDevice::ExeOther)) {
    s += "x";
  } else {
    s += "-";
  }

  return s;
}

QString makeDetails(const SpawnParameters& sp, DWORD code, const QString& more = {})
{
  STUB();
  return "";
}

QString makeContent(const SpawnParameters& sp, DWORD code, const QString& message = {})
{
  STUB();
  return QString(strerror(code));
}

QMessageBox::StandardButton badSteamPath(QWidget* parent)
{
  const auto details = QString("can't start steam, steam.desktop not exist");

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

DWORD spawn(const SpawnParameters& sp, HANDLE& processHandle)
{
  STUB();
  return ENOTSUP;
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

bool startSteam(QWidget* parent)
{
  STUB();
  return false;
}

HANDLE startBinary(QWidget* parent, const SpawnParameters& sp)
{
  HANDLE handle = INVALID_HANDLE_VALUE;
  const auto e  = spawn::spawn(sp, handle);

  switch (e) {
  case 0: {
    return handle;
  }

  case EACCES: {
    startBinaryAdmin(parent, sp);
    return INVALID_HANDLE_VALUE;
  }

  default: {
    dialogs::spawnFailed(parent, sp, e);
    return INVALID_HANDLE_VALUE;
  }
  }
}

std::pair<QString, int> getSteamExecutable(QWidget* parent)
{
  QString steam =
      QStandardPaths::locate(QStandardPaths::ApplicationsLocation, "steam.desktop");

  if (steam.isEmpty()) {
    return {{}, dialogs::badSteamPath(parent)};
  }

  return {"steam", 0};
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
    return tmp + "bin/java";
  }

  // not found
  return {};
}

bool isBatchFile(const QFileInfo& target)
{
  return target.suffix() == "sh";
}

bool isExeFile(const QFileInfo& target)
{
  return target.isExecutable() && target.isFile();
}

QFileInfo getCmdPath()
{
  return QFileInfo("/bin/sh");
}

extern bool isJavaFile(const QFileInfo& target);

FileExecutionContext getFileExecutionContext(QWidget* parent, const QFileInfo& target)
{
  if (isExeFile(target)) {
    return {target, "", FileExecutionTypes::Executable};
  }

  if (isBatchFile(target)) {
    return {
      getCmdPath(),
      QString("-c \"%1\"").arg(QDir::toNativeSeparators(target.absoluteFilePath())),
      FileExecutionTypes::Executable};
  }

  if (isJavaFile(target)) {
    auto java = findJavaInstallation(target.absoluteFilePath());

    if (java.isEmpty()) {
      java =
          QFileDialog::getOpenFileName(parent, QObject::tr("Select executable"), QString());
    }

    if (!java.isEmpty()) {
      return {QFileInfo(java),
              QString("-jar \"%1\"")
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
  const QString fileName = QDir(moDirectory).path() + "/helper";

  QProcess process;

  process.setWorkingDirectory(moDirectory);
  process.startCommand(fileName + " " + commandLine);

  if (!process.waitForStarted(1000)) {
    spawn::dialogs::helperFailed(parent, process.errorString(), "waitForStarted()",
                                 fileName, moDirectory, commandLine);

    return false;
  }

  if (async) {
    return true;
  }

  if (!process.waitForFinished()) {
    spawn::dialogs::helperFailed(parent, process.errorString(), "waitForFinished()",
                                 fileName, moDirectory, commandLine);

    return false;
  }

  int exitCode = process.exitCode();
  if (exitCode != 0) {
    spawn::dialogs::helperFailed(parent, process.errorString(), "exitCode()", fileName,
                                 moDirectory, commandLine);

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
