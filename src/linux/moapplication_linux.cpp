#include "settings.h"
#include "shared/util.h"

#include <QStandardPaths>
#include <log.h>

using namespace Qt::StringLiterals;
using namespace MOBase;
using namespace MOShared;

namespace
{

constexpr char icon[] = {
#embed "../resources/linux/ModOrganizer.svg"
};

constexpr char desktopFile[] = {
#embed "../resources/linux/ModOrganizer.desktop"
};

const QRegularExpression regex(u"Exec=(.*)\n"_s);

void fixExecField(QString& str)
{
  str.replace(regex, "Exec="_L1 % getApplicationFilePath() % '\n');
}

void writeDesktopFile(const QString& path, const QString& data)
{
  log::debug("writing desktop file");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    log::error("Error opening '{}' for writing: ", file.fileName(), file.errorString());
    return;
  }

  file.write(data.toUtf8());
}

void writeDesktopFile(const QString& path)
{
  QString data = QString::fromUtf8(desktopFile, sizeof(desktopFile));
  fixExecField(data);
  writeDesktopFile(path, data);
}

void writeIcon()
{
  QString iconPath =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) %
      "/icons/hicolor/scalable/apps"_L1;
  QString iconFilePath = iconPath % "/ModOrganizer.svg"_L1;
  QDir iconDir(iconPath);

  if (!iconDir.exists()) {
    log::debug("creating icon directory");
    std::error_code ec;
    std::filesystem::create_directories(iconDir.filesystemAbsolutePath(), ec);
    if (ec) {
      log::warn("error creating icon directory: {}", ec.message());
      return;
    }
  }

  if (!QFile::exists(iconFilePath)) {
    log::debug("writing icon");
    QFile iconFile(iconFilePath);
    if (!iconFile.open(QIODevice::WriteOnly)) {
      log::error("Error opening '{}' for writing: ", iconFile.fileName(),
                 iconFile.errorString());
      return;
    }
    iconFile.write(icon, sizeof(icon));
  }
}

bool checkIcon()
{
  QString iconFilePath =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) %
      "/icons/hicolor/scalable/apps/ModOrganizer.svg"_L1;
  QFile iconFile(iconFilePath);

  if (!iconFile.exists()) {
    return false;
  }
  if (!iconFile.open(QIODeviceBase::ReadOnly)) {
    log::error("Error opening icon file: {}", iconFile.errorString());
    return false;
  }

  const QByteArray iconFileData = iconFile.readAll();
  const QByteArray targetData   = QByteArray::fromRawData(icon, sizeof(icon));

  return iconFileData != targetData;
}

void createFiles()
{
  writeDesktopFile(
      QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) %
      "/ModOrganizer.desktop"_L1);
  writeIcon();
}

}  // namespace

// runtime libraries are found using rpath or runpath
// readelf -d <file> can be used to check those
// export LD_DEBUG=libs for debugging
void addDllsToPath()
{
  // no-op
}

void installDesktopFile()
{
  using enum GlobalSettings::DesktopFileAction;

  if (GlobalSettings::desktopFileDefaultAction() == ignore) {
    return;
  }

  auto desktopFileLocation = QStandardPaths::locate(
      QStandardPaths::ApplicationsLocation, u"ModOrganizer.desktop"_s);
  if (desktopFileLocation.isEmpty()) {
    if (GlobalSettings::desktopFileDefaultAction() == installOrUpdate) {
      log::debug("No desktop file found and default action is `installOrUpdate`. "
                 "Creating it without asking the user");
      createFiles();
      return;
    }
    log::debug("No desktop file found, asking user to install it");
    auto* dialog = new QMessageBox(
        QMessageBox::Icon::Question,
        QObject::tr(
            "Add ModOrganizer to your application launcher? (write desktop file)"),
        QObject::tr("ModOrganizer is not currently registered with your desktop "
                    "environment.\n\n"
                    "Adding a launcher entry enables features such as taskbar download "
                    "progress "
                    "and makes ModOrganizer appear in your application launcher.\n\n"
                    "This is recommended for most users. Do you want to add it now?"));
    const auto* yesButton =
        dialog->addButton(QObject::tr("Install"), QMessageBox::YesRole);
    dialog->addButton(QObject::tr("Not now"), QMessageBox::NoRole);
    dialog->setCheckBox(new QCheckBox(QObject::tr("Don't ask again")));

    QObject::connect(dialog, &QMessageBox::buttonClicked,
                     [=](const QAbstractButton* button) {
                       if (button == yesButton) {
                         createFiles();
                       } else {
                         log::debug("User declined installing the desktop file");
                       }
                       if (dialog->checkBox()->isChecked()) {
                         GlobalSettings::setDesktopFileDefaultAction(
                             button == yesButton ? installOrUpdate : ignore);
                       }
                     });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
  } else {
    // check if the desktop file should be updated. this can be necessary if the user
    // has run ModOrganizer from a different location (Exec field has to be updated) or
    // to apply any changes.
    QFile file(desktopFileLocation);
    if (!file.open(QIODeviceBase::ReadOnly)) {
      log::error("Error opening '{}': ", file.fileName(), file.errorString());
      return;
    }

    const QByteArray data = file.readAll();
    QString dataString    = QString::fromUtf8(data);
    fixExecField(dataString);

    QString targetDataString = QString::fromUtf8(desktopFile, std::size(desktopFile));
    fixExecField(targetDataString);
    if (targetDataString == dataString) {
      return;
    }

    const QRegularExpressionMatch match = regex.match(dataString);

    bool shouldWrite = false;
    QString reason;
    if (match.hasMatch()) {
      if (match.captured(1) != getApplicationFilePath()) {
        log::debug("Found an issue in the desktop file: The Exec field does not match "
                   "the application path");
        shouldWrite = true;
        reason = QObject::tr("The existing ModOrganizer application launcher entry "
                             "(desktop file) points to a different executable.");
      }
    } else {
      log::debug("Found an issue in the desktop file: It either has to get updated or "
                 "may be corrupt");
      shouldWrite = true;
      reason      = QObject::tr("The existing ModOrganizer application launcher entry "
                                "(desktop file) appears to be outdated "
                                "or could not be recognized.");
    }

    if (shouldWrite) {
      if (GlobalSettings::desktopFileDefaultAction() == installOrUpdate) {
        log::debug(
            "Default action is `installOrUpdate`. Rewriting without asking the user");
        writeDesktopFile(desktopFileLocation, dataString);
        if (!checkIcon()) {
          log::debug("There was something wrong with the application icon in addition "
                     "to the desktop file, rewriting it");
          writeIcon();
        }
        return;
      }

      auto* dialog = new QMessageBox(
          QMessageBox::Icon::Question,
          QObject::tr("Update ModOrganizer application launcher entry (desktop file)?"),
          QObject::tr("%1\n\nDo you want to update it now?").arg(reason));
      const auto* yesButton =
          dialog->addButton(QObject::tr("Update"), QMessageBox::YesRole);
      dialog->addButton(QObject::tr("Keep existing"), QMessageBox::NoRole);
      dialog->setCheckBox(new QCheckBox(QObject::tr("Don't ask again")));

      QObject::connect(
          dialog, &QMessageBox::buttonClicked, [=](QAbstractButton* button) {
            if (button == yesButton) {
              writeDesktopFile(desktopFileLocation, dataString);
              if (!checkIcon()) {
                log::debug("There was something wrong with the application icon in "
                           "addition to the desktop file, rewriting it");
                writeIcon();
              }
            } else {
              log::debug("User declined rewriting the desktop file");
            }
            if (dialog->checkBox()->isChecked()) {
              GlobalSettings::setDesktopFileDefaultAction(
                  button == yesButton ? installOrUpdate : ignore);
            }
          });
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->show();
    }
  }
}
