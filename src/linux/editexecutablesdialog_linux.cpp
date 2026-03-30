#include "editexecutablesdialog.h"
#include "filedialogmemory.h"
#include "organizercore.h"
#include "ui_editexecutablesdialog.h"

using namespace MOBase;

void EditExecutablesDialog::on_browsePrefixDirectory_clicked()
{
  QString dirName = FileDialogMemory::getExistingDirectory(
      "editPrefixDirectory", this, tr("Select a directory"), ui->prefixDir->text());

  if (dirName.isNull()) {
    // cancelled
    return;
  }

  ui->prefixDir->setText(dirName);
}
