#include "createinstancedialogpages.h"
#include "ui_createinstancedialog.h"

#include <QFile>
#include <QFileDialog>
#include <iplugingame.h>
#include <log.h>
#include <steamutility.h>

namespace cid
{

using namespace MOBase;
using namespace Qt::StringLiterals;

QString Page::selectedGamePrefix() const
{
  // no-op
  return {};
}

QString PrefixPage::selectedGamePrefix() const
{
  if (!m_okay) {
    return {};
  }

  return ui->prefixDir->text();
}

void PrefixPage::doActivated(bool firstTime)
{
  auto* g = m_dlg.rawCreationInfo().game;
  if (!g) {
    // shouldn't happen, next should be disabled
    return;
  }

  if (firstTime) {
    QString prefixDir = findCompatDataByAppID(g->steamAPPId());
    if (prefixDir.isEmpty() || !QFile::exists(prefixDir)) {
      log::warn("Error determining wine prefix from steam app ID");
    } else {
      ui->prefixDir->setText(prefixDir);
    }
  }

  verify();
}

PrefixPage::PrefixPage(CreateInstanceDialog& dlg)
    : Page(dlg), m_modified(false), m_okay(false)
{
  auto setEdit = [&](QLineEdit* e) {
    QObject::connect(e, &QLineEdit::textEdited, [&] {
      onChanged();
    });
    QObject::connect(e, &QLineEdit::returnPressed, [&] {
      next();
    });
  };

  auto setBrowse = [&](QAbstractButton* b, QLineEdit* e) {
    QObject::connect(b, &QAbstractButton::clicked, [this, e] {
      browse(e);
    });
  };

  setEdit(ui->prefixDir);
  setBrowse(ui->browsePrefix, ui->prefixDir);
}

bool PrefixPage::ready() const
{
  return m_okay;
}

void PrefixPage::onChanged()
{
  m_modified = true;
  verify();
}

void PrefixPage::verify()
{
  // path should either be an empty directory or a valid prefix

  const QString selectedPrefixDir = ui->prefixDir->text();

  if (selectedPrefixDir.isEmpty()) {
    ui->prefixDoesNotExist->setVisible(false);
    ui->prefixInvalid->setVisible(true);
    m_okay = false;
  } else if (!QFile::exists(selectedPrefixDir)) {
    ui->prefixDoesNotExist->setVisible(true);
    ui->prefixInvalid->setVisible(false);
    m_okay = false;
  } else {
    if (QDir(selectedPrefixDir)
            .entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)
            .isEmpty() ||
        QFile::exists(selectedPrefixDir % "/pfx/drive_c"_L1) ||
        QFile::exists(selectedPrefixDir % "/drive_c"_L1)) {
      ui->prefixDoesNotExist->setVisible(false);
      ui->prefixInvalid->setVisible(false);
      m_okay = true;
    } else {
      ui->prefixDoesNotExist->setVisible(false);
      ui->prefixInvalid->setVisible(true);
      m_okay = false;
    }
  }

  updateNavigation();
}

void PrefixPage::browse(QLineEdit* e) const
{
  const auto s = QFileDialog::getExistingDirectory(&m_dlg, {}, e->text());
  if (s.isNull() || s.isEmpty()) {
    return;
  }

  e->setText(s);
}

}  // namespace cid
