/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

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

#include "categories.h"

#include <log.h>
#include <report.h>
#include <utility.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QList>
#include <QObject>

#include "nexusinterface.h"

using namespace MOBase;
using namespace Qt::StringLiterals;

CategoryFactory* CategoryFactory::s_Instance = nullptr;

QString CategoryFactory::categoriesFilePath()
{
  return qApp->property("dataPath").toString() % u"/categories.dat"_s;
}

CategoryFactory::CategoryFactory() : QObject()
{
  atexit(&cleanup);
}

QString CategoryFactory::nexusMappingFilePath()
{
  return qApp->property("dataPath").toString() % u"/nexuscatmap.dat"_s;
}

void CategoryFactory::loadCategories()
{
  reset();

  QFile categoryFile(categoriesFilePath());
  bool needLoad = false;

  if (!categoryFile.open(QIODevice::ReadOnly)) {
    needLoad = true;
  } else {
    int lineNum = 0;
    while (!categoryFile.atEnd()) {
      QByteArray line = categoryFile.readLine();
      ++lineNum;
      QList<QByteArray> cells = line.split('|');
      if (cells.count() == 4) {
        std::vector<NexusCategory> nexusCats;
        if (cells[2].length() > 0) {
          QList<QByteArray> nexusIDStrings = cells[2].split(',');
          for (QList<QByteArray>::iterator iter = nexusIDStrings.begin();
               iter != nexusIDStrings.end(); ++iter) {
            bool ok  = false;
            int temp = iter->toInt(&ok);
            if (!ok) {
              log::error(tr("invalid category id {0}"), iter->constData());
            }
            nexusCats.push_back(NexusCategory(u"Unknown"_s, temp));
          }
        }
        bool cell0Ok = true;
        bool cell3Ok = true;
        int id       = cells[0].toInt(&cell0Ok);
        int parentID = cells[3].trimmed().toInt(&cell3Ok);
        if (!cell0Ok || !cell3Ok) {
          log::error(tr("invalid category line {0}: {1}"), lineNum, line.constData());
        }
        addCategory(id, QString::fromUtf8(cells[1].constData()), nexusCats, parentID);
      } else if (cells.count() == 3) {
        bool cell0Ok = true;
        bool cell3Ok = true;
        int id       = cells[0].toInt(&cell0Ok);
        int parentID = cells[2].trimmed().toInt(&cell3Ok);
        if (!cell0Ok || !cell3Ok) {
          log::error(tr("invalid category line {0}: {1}"), lineNum, line.constData());
        }

        addCategory(id, QString::fromUtf8(cells[1].constData()),
                    std::vector<NexusCategory>(), parentID);
      } else {
        log::error(tr("invalid category line {0}: {1} ({2} cells)"), lineNum,
                   line.constData(), cells.count());
      }
    }
    categoryFile.close();

    QFile nexusMapFile(nexusMappingFilePath());
    if (!nexusMapFile.open(QIODevice::ReadOnly)) {
      needLoad = true;
    } else {
      int nexLineNum = 0;
      while (!nexusMapFile.atEnd()) {
        QByteArray nexLine = nexusMapFile.readLine();
        ++nexLineNum;
        QList<QByteArray> nexCells = nexLine.split('|');
        if (nexCells.count() == 3) {
          std::vector<NexusCategory> nexusCats;
          QString nexName = nexCells[1];
          bool ok         = false;
          int nexID       = nexCells[2].toInt(&ok);
          if (!ok) {
            log::error(tr("invalid nexus ID {}"), nexCells[2].constData());
          }
          int catID = nexCells[0].toInt(&ok);
          if (!ok) {
            log::error(tr("invalid category id {}"), nexCells[0].constData());
          }
          m_NexusMap.insert_or_assign(nexID, NexusCategory(nexName, nexID));
          m_NexusMap.at(nexID).setCategoryID(catID);
        } else {
          log::error(tr("invalid nexus category line {0}: {1} ({2} cells)"), lineNum,
                     nexLine.constData(), nexCells.count());
        }
      }
    }
    nexusMapFile.close();
  }
  std::sort(m_Categories.begin(), m_Categories.end());
  setParents();
  if (needLoad)
    loadDefaultCategories();
}

CategoryFactory& CategoryFactory::instance()
{
  static CategoryFactory s_Instance;
  return s_Instance;
}

void CategoryFactory::reset()
{
  m_Categories.clear();
  m_NexusMap.clear();
  m_IDMap.clear();
  addCategory(0, u"None"_s, std::vector<NexusCategory>(), 0);
}

void CategoryFactory::setParents()
{
  for (auto& category : m_Categories) {
    category.setHasChildren(false);
  }

  for (const auto& category : m_Categories) {
    if (category.parentID() != 0) {
      std::map<int, unsigned int>::const_iterator iter =
          m_IDMap.find(category.parentID());
      if (iter != m_IDMap.end()) {
        m_Categories[iter->second].setHasChildren(true);
      }
    }
  }
}

void CategoryFactory::cleanup()
{
  delete s_Instance;
  s_Instance = nullptr;
}

void CategoryFactory::saveCategories()
{
  QFile categoryFile(categoriesFilePath());

  if (!categoryFile.open(QIODevice::WriteOnly)) {
    reportError(tr("Failed to save custom categories"));
    return;
  }

  categoryFile.resize(0);
  for (const auto& category : m_Categories) {
    if (category.ID() == 0) {
      continue;
    }
    QByteArray line;
    line.append(QByteArray::number(category.ID()))
        .append("|")
        .append(category.name().toUtf8())
        .append("|")
        .append(QByteArray::number(category.parentID()))
        .append("\n");
    categoryFile.write(line);
  }
  categoryFile.close();

  QFile nexusMapFile(nexusMappingFilePath());

  if (!nexusMapFile.open(QIODevice::WriteOnly)) {
    reportError(tr("Failed to save nexus category mappings"));
    return;
  }

  nexusMapFile.resize(0);
  for (const auto& nexMap : m_NexusMap) {
    QByteArray line;
    line.append(QByteArray::number(nexMap.second.categoryID())).append("|");
    line.append(nexMap.second.name().toUtf8()).append("|");
    line.append(QByteArray::number(nexMap.second.ID())).append("\n");
    nexusMapFile.write(line);
  }
  nexusMapFile.close();

  emit categoriesSaved();
}

unsigned int
CategoryFactory::countCategories(std::function<bool(const Category& category)> filter)
{
  unsigned int result = 0;
  for (const Category& cat : m_Categories) {
    if (filter(cat)) {
      ++result;
    }
  }
  return result;
}

int CategoryFactory::addCategory(const QString& name,
                                 const std::vector<NexusCategory>& nexusCats,
                                 int parentID)
{
  int id = 1;
  while (m_IDMap.find(id) != m_IDMap.end()) {
    ++id;
  }
  addCategory(id, name, nexusCats, parentID);

  saveCategories();
  return id;
}

void CategoryFactory::addCategory(int id, const QString& name, int parentID)
{
  int index = static_cast<int>(m_Categories.size());
  m_Categories.push_back(
      Category(index, id, name, parentID, std::vector<NexusCategory>()));
  m_IDMap[id] = index;
}

void CategoryFactory::addCategory(int id, const QString& name,
                                  const std::vector<NexusCategory>& nexusCats,
                                  int parentID)
{
  for (const auto& nexusCat : nexusCats) {
    m_NexusMap.insert_or_assign(nexusCat.ID(), nexusCat);
    m_NexusMap.at(nexusCat.ID()).setCategoryID(id);
  }
  int index = static_cast<int>(m_Categories.size());
  m_Categories.push_back(Category(index, id, name, parentID, nexusCats));
  m_IDMap[id] = index;
}

void CategoryFactory::setNexusCategories(
    const std::vector<CategoryFactory::NexusCategory>& nexusCats)
{
  for (const auto& nexusCat : nexusCats) {
    m_NexusMap.emplace(nexusCat.ID(), nexusCat);
  }

  saveCategories();
}

void CategoryFactory::refreshNexusCategories(CategoriesDialog* dialog)
{
  emit nexusCategoryRefresh(dialog);
}

void CategoryFactory::loadDefaultCategories()
{
  // the order here is relevant as it defines the order in which the
  // mods appear in the combo box
  addCategory(1, u"Animations"_s, 0);
  addCategory(52, u"Poses"_s, 1);
  addCategory(2, u"Armour"_s, 0);
  addCategory(53, u"Power Armor"_s, 2);
  addCategory(3, u"Audio"_s, 0);
  addCategory(38, u"Music"_s, 0);
  addCategory(39, u"Voice"_s, 0);
  addCategory(5, u"Clothing"_s, 0);
  addCategory(41, u"Jewelry"_s, 5);
  addCategory(42, u"Backpacks"_s, 5);
  addCategory(6, u"Collectables"_s, 0);
  addCategory(28, u"Companions"_s, 0);
  addCategory(7, u"Creatures, Mounts, & Vehicles"_s, 0);
  addCategory(8, u"Factions"_s, 0);
  addCategory(9, u"Gameplay"_s, 0);
  addCategory(27, u"Combat"_s, 9);
  addCategory(43, u"Crafting"_s, 9);
  addCategory(48, u"Overhauls"_s, 9);
  addCategory(49, u"Perks"_s, 9);
  addCategory(54, u"Radio"_s, 9);
  addCategory(55, u"Shouts"_s, 9);
  addCategory(22, u"Skills & Levelling"_s, 9);
  addCategory(58, u"Weather & Lighting"_s, 9);
  addCategory(44, u"Equipment"_s, 43);
  addCategory(45, u"Home/Settlement"_s, 43);
  addCategory(10, u"Body, Face, & Hair"_s, 0);
  addCategory(39, u"Tattoos"_s, 10);
  addCategory(40, u"Character Presets"_s, 0);
  addCategory(11, u"Items"_s, 0);
  addCategory(32, u"Mercantile"_s, 0);
  addCategory(37, u"Ammo"_s, 11);
  addCategory(19, u"Weapons"_s, 11);
  addCategory(36, u"Weapon & Armour Sets"_s, 11);
  addCategory(23, u"Player Homes"_s, 0);
  addCategory(25, u"Castles & Mansions"_s, 23);
  addCategory(51, u"Settlements"_s, 23);
  addCategory(12, u"Locations"_s, 0);
  addCategory(4, u"Cities"_s, 12);
  addCategory(31, u"Landscape Changes"_s, 0);
  addCategory(29, u"Environment"_s, 0);
  addCategory(30, u"Immersion"_s, 0);
  addCategory(20, u"Magic"_s, 0);
  addCategory(21, u"Models & Textures"_s, 0);
  addCategory(33, u"Modders resources"_s, 0);
  addCategory(13, u"NPCs"_s, 0);
  addCategory(24, u"Bugfixes"_s, 0);
  addCategory(14, u"Patches"_s, 24);
  addCategory(35, u"Utilities"_s, 0);
  addCategory(26, u"Cheats"_s, 0);
  addCategory(15, u"Quests"_s, 0);
  addCategory(16, u"Races & Classes"_s, 0);
  addCategory(34, u"Stealth"_s, 0);
  addCategory(17, u"UI"_s, 0);
  addCategory(18, u"Visuals"_s, 0);
  addCategory(50, u"Pip-Boy"_s, 18);
  addCategory(46, u"Shader Presets"_s, 0);
  addCategory(47, u"Miscellaneous"_s, 0);
}

int CategoryFactory::getParentID(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].parentID();
}

bool CategoryFactory::categoryExists(int id) const
{
  return m_IDMap.find(id) != m_IDMap.end();
}

bool CategoryFactory::isDescendantOf(int id, int parentID) const
{
  // handles cycles
  std::set<int> seen;
  return isDescendantOfImpl(id, parentID, seen);
}

bool CategoryFactory::isDescendantOfImpl(int id, int parentID,
                                         std::set<int>& seen) const
{
  if (!seen.insert(id).second) {
    log::error("cycle in category: {}", id);
    return false;
  }

  std::map<int, unsigned int>::const_iterator iter = m_IDMap.find(id);

  if (iter != m_IDMap.end()) {
    unsigned int index = iter->second;
    if (m_Categories[index].parentID() == 0) {
      return false;
    } else if (m_Categories[index].parentID() == parentID) {
      return true;
    } else {
      return isDescendantOfImpl(m_Categories[index].parentID(), parentID, seen);
    }
  } else {
    log::warn(tr("{} is no valid category id"), id);
    return false;
  }
}

bool CategoryFactory::hasChildren(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].hasChildren();
}

QString CategoryFactory::getCategoryName(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].name();
}

QString CategoryFactory::getSpecialCategoryName(SpecialCategories type) const
{
  QString label;
  switch (type) {
  case Checked:
    label = QObject::tr("Active");
    break;
  case UpdateAvailable:
    label = QObject::tr("Update available");
    break;
  case HasCategory:
    label = QObject::tr("Has category");
    break;
  case Conflict:
    label = QObject::tr("Conflicted");
    break;
  case HasHiddenFiles:
    label = QObject::tr("Has hidden files");
    break;
  case Endorsed:
    label = QObject::tr("Endorsed");
    break;
  case Backup:
    label = QObject::tr("Has backup");
    break;
  case Managed:
    label = QObject::tr("Managed");
    break;
  case HasGameData:
    label = QObject::tr("Has valid game data");
    break;
  case HasNexusID:
    label = QObject::tr("Has Nexus ID");
    break;
  case Tracked:
    label = QObject::tr("Tracked on Nexus");
    break;
  default:
    return {};
  }
  return QStringLiteral("<%1>").arg(label);
}

QString CategoryFactory::getCategoryNameByID(int id) const
{
  auto itor = m_IDMap.find(id);

  if (itor == m_IDMap.end()) {
    return getSpecialCategoryName(static_cast<SpecialCategories>(id));
  } else {
    const auto index = itor->second;
    if (index >= m_Categories.size()) {
      return {};
    }

    return m_Categories[index].name();
  }
}

int CategoryFactory::getCategoryID(unsigned int index) const
{
  if (index >= m_Categories.size()) {
    throw MyException(tr("invalid category index: %1").arg(index));
  }

  return m_Categories[index].ID();
}

int CategoryFactory::getCategoryIndex(int ID) const
{
  std::map<int, unsigned int>::const_iterator iter = m_IDMap.find(ID);
  if (iter == m_IDMap.end()) {
    throw MyException(tr("invalid category id: %1").arg(ID));
  }
  return iter->second;
}

int CategoryFactory::getCategoryID(const QString& name) const
{
  auto iter = std::find_if(m_Categories.begin(), m_Categories.end(),
                           [name](const Category& cat) -> bool {
                             return cat.name() == name;
                           });

  if (iter != m_Categories.end()) {
    return iter->ID();
  } else {
    return -1;
  }
}

unsigned int CategoryFactory::resolveNexusID(int nexusID) const
{
  auto result = m_NexusMap.find(nexusID);
  if (result != m_NexusMap.end()) {
    if (m_IDMap.count(result->second.categoryID())) {
      log::debug(tr("nexus category id {0} maps to internal {1}"), nexusID,
                 m_IDMap.at(result->second.categoryID()));
      return m_IDMap.at(result->second.categoryID());
    }
  }
  log::debug(tr("nexus category id {} not mapped"), nexusID);
  return 0U;
}
