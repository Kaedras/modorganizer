#include "shared/util.h"

#include <QStandardPaths>
#include <log.h>

using namespace Qt::StringLiterals;
using namespace MOBase;

namespace
{

constexpr char icon[] = {
#embed "../resources/linux/ModOrganizer.svg"
};

constexpr char desktopFile[] = {
#embed "../resources/linux/ModOrganizer.desktop"
};

const QRegularExpression regex(u"Exec=(.*)\n"_s);

void writeDesktopFile(const QString& path)
{
  log::debug("writing desktop file");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    log::error("Error opening '{}' for writing: ", file.fileName(), file.errorString());
    return;
  }

  QString desktopFileContent = QString::fromUtf8(desktopFile, sizeof(desktopFile));

  // fix the Exec field
  desktopFileContent.replace(regex,
                             "Exec="_L1 % MOShared::getApplicationFilePath() % "\n");

  file.write(desktopFileContent.toLocal8Bit());
}

void writeIcon()
{
  QString iconPath =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) %
      "/icons/hicolor/scalable/apps/ModOrganizer.svg"_L1;
  if (!QFile::exists(iconPath)) {
    log::debug("writing icon");
    QFile iconFile(iconPath);
    if (!iconFile.open(QIODevice::WriteOnly)) {
      log::error("Error opening '{}' for writing: ", iconFile.fileName(),
                 iconFile.errorString());
      return;
    }
    iconFile.write(icon, sizeof(icon));
  }
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
  auto desktopFileLocation = QStandardPaths::locate(
      QStandardPaths::ApplicationsLocation, u"ModOrganizer.desktop"_s);
  if (desktopFileLocation.isEmpty()) {
    log::debug("No desktop file found, prompting user to install one");
    auto* dialog = new QMessageBox(
        QMessageBox::Icon::Question, QObject::tr("Install desktop file?"),
        QObject::tr("No ModOrganizer desktop file has been detected.\nWould you like "
                    "to install one?"),
        QMessageBox::Yes | QMessageBox::No);
    QObject::connect(dialog, &QMessageBox::buttonClicked, [=](QAbstractButton* button) {
      if (dialog->standardButton(button) == QMessageBox::Yes) {
        writeDesktopFile(
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation) %
            "/ModOrganizer.desktop"_L1);
        writeIcon();
      } else {
        log::debug("user denied installing the desktop file");
      }
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
  } else {
    // check if the Exec field inside the desktop file should be updated as a mismatch
    // can cause dbus errors
    QFile file(desktopFileLocation);
    if (!file.open(QIODeviceBase::ReadOnly)) {
      log::error("Error opening '{}': ", file.fileName(), file.errorString());
      return;
    }
    const QString data            = QString::fromUtf8(file.readAll());
    QRegularExpressionMatch match = regex.match(data);

    bool shouldWrite = false;

    if (!match.hasMatch()) {
      log::debug("prompting user to rewrite desktop file because it may be corrupt");
      shouldWrite = true;
    } else {
      if (match.captured(1) != MOShared::getApplicationFilePath()) {
        log::debug("prompting user to rewrite desktop file because the Exec field does "
                   "not match the application path");
        shouldWrite = true;
      }
    }

    if (shouldWrite) {
      auto* dialog = new QMessageBox(
          QMessageBox::Icon::Question, QObject::tr("Fix desktop file?"),
          QObject::tr("A ModOrganizer desktop file has been found, but refers to "
                      "another executable.\nShould it be rewritten?"),
          QMessageBox::Yes | QMessageBox::No);
      QObject::connect(dialog, &QMessageBox::buttonClicked,
                       [=](QAbstractButton* button) {
                         if (dialog->standardButton(button) == QMessageBox::Yes) {
                           writeDesktopFile(desktopFileLocation);
                         } else {
                           log::debug("user denied rewriting the desktop file");
                         }
                       });
      dialog->setAttribute(Qt::WA_DeleteOnClose);
      dialog->show();
    }
  }
}
