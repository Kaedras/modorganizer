#include "filetree.h"
#include "filetreeitem.h"
#include "filetreemodel.h"
#include "organizercore.h"
#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/filesorigin.h"
#include <log.h>
#include <widgetutility.h>

using namespace MOShared;
using namespace MOBase;


bool FileTree::showShellMenu(QPoint pos)
{
  auto* mw = getMainWindow(m_tree);

  // menus by origin
  std::map<int, env::ShellMenu> menus;
  int totalFiles   = 0;
  bool warnOnEmpty = true;

  for (auto&& index : m_tree->selectionModel()->selectedRows()) {
    auto* item = m_model->itemFromIndex(proxiedIndex(index));
    if (!item) {
      continue;
    }

    if (item->isDirectory()) {
      warnOnEmpty = false;

      log::warn("directories do not have shell menus; '{}' selected", item->filename());

      continue;
    }

    if (item->isFromArchive()) {
      warnOnEmpty = false;

      log::warn("files from archives do not have shell menus; '{}' selected",
                item->filename());

      continue;
    }

    auto itor = menus.find(item->originID());
    if (itor == menus.end()) {
      itor = menus.emplace(item->originID(), mw).first;
    }

    if (!QFile::exists(item->realPath())) {
      log::error("{}", tr("File '%1' does not exist, you may need to refresh.")
                           .arg(item->realPath()));
    }

    itor->second.addFile(QFileInfo(item->realPath()));
    ++totalFiles;

    if (item->isConflicted()) {
      const auto file = m_core.directoryStructure()->searchFile(
          item->dataRelativeFilePath(), nullptr);

      if (!file) {
        log::error("file '{}' not found, data path={}, real path={}", item->filename(),
                   item->dataRelativeFilePath(), item->realPath());

        continue;
      }

      const auto alts = file->getAlternatives();
      if (alts.empty()) {
        log::warn("file '{}' has no alternative origins but is marked as conflicted",
                  item->dataRelativeFilePath());
      }

      for (auto&& alt : alts) {
        auto itor = menus.find(alt.originID());
        if (itor == menus.end()) {
          itor = menus.emplace(alt.originID(), mw).first;
        }

        const auto fullPath = file->getFullPath(alt.originID());
        if (fullPath.isEmpty()) {
          log::error("file {} not found in origin {}", item->dataRelativeFilePath(),
                     alt.originID());

          continue;
        }

        if (!QFile::exists(fullPath)) {
          log::error("{}", tr("File '%1' does not exist, you may need to refresh.")
                               .arg(fullPath));
        }

        itor->second.addFile(QFileInfo(fullPath));
      }
    }
  }

  if (menus.empty()) {
    // don't warn if something that doesn't have a shell menu was selected, a
    // warning has already been logged above
    if (warnOnEmpty) {
      log::warn("no menus to show");
    }

    return false;
  } else if (menus.size() == 1) {
    auto& menu = menus.begin()->second;
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
  } else {
    env::ShellMenuCollection mc(mw);
    bool hasDiscrepancies = false;

    for (auto&& m : menus) {
      const auto* origin = m_core.directoryStructure()->findOriginByID(m.first);
      if (!origin) {
        log::error("origin {} not found for merged menus", m.first);
        continue;
      }

      QString caption = QString::fromStdWString(origin->getName());
      if (m.second.fileCount() < totalFiles) {
        const auto d = m.second.fileCount();
        caption += " " + tr("(only has %1 file(s))").arg(d);
        hasDiscrepancies = true;
      }

      mc.add(caption, std::move(m.second));
    }

    if (hasDiscrepancies) {
      mc.addDetails(tr("%1 file(s) selected").arg(totalFiles));
    }

    mc.exec(m_tree->viewport()->mapToGlobal(pos));
  }

  return true;
}