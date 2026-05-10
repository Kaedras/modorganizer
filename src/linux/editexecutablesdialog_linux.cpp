#include "editexecutablesdialog.h"
#include "filedialogmemory.h"
#include "organizercore.h"
#include "ui_editexecutablesdialog.h"

using namespace MOBase;
using namespace Qt::StringLiterals;

void EditExecutablesDialog::on_browsePrefixDirectory_clicked()
{
  QString dirName = FileDialogMemory::getExistingDirectory(
      u"editPrefixDirectory"_s, this, tr("Select a directory"),
      ui->prefixDirectory->text());

  if (dirName.isNull()) {
    // cancelled
    return;
  }

  ui->prefixDirectory->setText(dirName);
}
