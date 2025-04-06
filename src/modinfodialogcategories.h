#ifndef MODINFODIALOGCATEGORIES_H
#define MODINFODIALOGCATEGORIES_H

#include "modinfodialogtab.h"

#include <set>

class QTreeWidgetItem;
class CategoryFactory;

class CategoriesTab : public ModInfoDialogTab
{
public:
  CategoriesTab(ModInfoDialogTabContext cx);

  void clear() override;
  void update() override;
  bool canHandleSeparators() const override;
  bool usesOriginFiles() const override;

private:
  void add(const CategoryFactory& factory, const std::set<int>& enabledCategories,
           QTreeWidgetItem* root, int rootLevel);

  void updatePrimary();
  void addChecked(QTreeWidgetItem* tree);

  void save(QTreeWidgetItem* currentNode);

  void onCategoryChanged(QTreeWidgetItem* item, int col);
  void onPrimaryChanged(int index);
};

#endif  //  MODINFODIALOGCATEGORIES_H