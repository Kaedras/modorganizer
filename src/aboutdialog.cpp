/*
Copyright (C) 2014 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "aboutdialog.h"
#include "shared/util.h"
#include "ui_aboutdialog.h"
#include <utility.h>

#include <QApplication>
#include <QFontDatabase>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTextBrowser>
#include <QVariant>
#include <Qt>

using namespace Qt::StringLiterals;

AboutDialog::AboutDialog(const QString& version, QWidget* parent)
    : QDialog(parent), ui(new Ui::AboutDialog)
{
  ui->setupUi(this);

  m_LicenseFiles[LICENSE_LGPL3]   = u"LGPL-v3.0.txt"_s;
  m_LicenseFiles[LICENSE_LGPL21]  = u"GNU-LGPL-v2.1.txt"_s;
  m_LicenseFiles[LICENSE_GPL3]    = u"GPL-v3.0.txt"_s;
  m_LicenseFiles[LICENSE_GPL2]    = u"GPL-v2.0.txt"_s;
  m_LicenseFiles[LICENSE_BOOST]   = u"boost.txt"_s;
  m_LicenseFiles[LICENSE_7ZIP]    = u"7zip.txt"_s;
  m_LicenseFiles[LICENSE_CCBY3]   = u"BY-SA-v3.0.txt"_s;
  m_LicenseFiles[LICENSE_ZLIB]    = u"zlib.txt"_s;
  m_LicenseFiles[LICENSE_PYTHON]  = u"python.txt"_s;
  m_LicenseFiles[LICENSE_SSL]     = u"openssl.txt"_s;
  m_LicenseFiles[LICENSE_CPPTOML] = u"cpptoml.txt"_s;
  m_LicenseFiles[LICENSE_UDIS]    = u"udis86.txt"_s;
  m_LicenseFiles[LICENSE_SPDLOG]  = u"spdlog.txt"_s;
  m_LicenseFiles[LICENSE_FMT]     = u"fmt.txt"_s;
  m_LicenseFiles[LICENSE_SIP]     = u"sip.txt"_s;
  m_LicenseFiles[LICENSE_CASTLE]  = u"Castle.txt"_s;
  m_LicenseFiles[LICENSE_ANTLR]   = u"AntlrBuildTask.txt"_s;
  m_LicenseFiles[LICENSE_DXTEX]   = u"DXTex.txt"_s;
  m_LicenseFiles[LICENSE_VDF]     = u"ValveFileVDF.txt"_s;

  addLicense(u"Qt"_s, LICENSE_LGPL3);
  addLicense(u"Qt Json"_s, LICENSE_GPL3);
  addLicense(u"Boost Library"_s, LICENSE_BOOST);
  addLicense(u"7-zip"_s, LICENSE_7ZIP);
  addLicense(u"ZLib"_s, LICENSE_NONE);
  addLicense(u"Tango Icon Theme"_s, LICENSE_NONE);
  addLicense(u"RRZE Icon Set"_s, LICENSE_CCBY3);
  addLicense(u"Icons by Lorc, Delapouite and sbed available on http://game-icons.net"_s,
             LICENSE_CCBY3);
  addLicense(u"Castle Core"_s, LICENSE_CASTLE);
  addLicense(u"ANTLR"_s, LICENSE_ANTLR);
  addLicense(u"LOOT"_s, LICENSE_GPL3);
  addLicense(u"Python"_s, LICENSE_PYTHON);
  addLicense(u"OpenSSL"_s, LICENSE_SSL);
  addLicense(u"cpptoml"_s, LICENSE_CPPTOML);
  addLicense(u"Udis86"_s, LICENSE_UDIS);
  addLicense(u"spdlog"_s, LICENSE_SPDLOG);
  addLicense(u"{fmt}"_s, LICENSE_FMT);
  addLicense(u"SIP"_s, LICENSE_SIP);
  addLicense(u"DXTex Headers"_s, LICENSE_DXTEX);
  addLicense(u"Valve File VDF Reader"_s, LICENSE_VDF);

  ui->nameLabel->setText(
      QStringLiteral("<span style=\"font-size:12pt; font-weight:600;\">%1 %2</span>")
          .arg(ui->nameLabel->text(), version));
#if defined(HGID)
  ui->revisionLabel->setText(ui->revisionLabel->text() % ' ' % HGID);
#elif defined(GITID)
  ui->revisionLabel->setText(ui->revisionLabel->text() % ' ' % GITID);
#else
  ui->revisionLabel->setText(ui->revisionLabel->text() % u" unknown"_s);
#endif

  ui->usvfsLabel->setText(ui->usvfsLabel->text() + " " +
                          MOShared::getUsvfsVersionString());
  ui->licenseText->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

AboutDialog::~AboutDialog()
{
  delete ui;
}

void AboutDialog::addLicense(const QString& name, Licenses license)
{
  QListWidgetItem* item = new QListWidgetItem(name);
  item->setData(Qt::UserRole, license);
  ui->creditsList->addItem(item);
}

void AboutDialog::on_creditsList_currentItemChanged(QListWidgetItem* current,
                                                    QListWidgetItem*)
{
  auto iter = m_LicenseFiles.find(current->data(Qt::UserRole).toInt());
  if (iter != m_LicenseFiles.end()) {
    QString filePath = qApp->applicationDirPath() % u"/licenses/"_s % iter->second;
    QString text     = MOBase::readFileText(filePath);
    ui->licenseText->setText(text);
  } else {
    ui->licenseText->setText(tr("No license"));
  }
}

void AboutDialog::on_sourceText_linkActivated(const QString& link)
{
  MOBase::shell::Open(QUrl(link));
}
