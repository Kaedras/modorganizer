#include "settingsutilities.h"
#include "expanderwidget.h"
#include <utility.h>

using namespace MOBase;

bool shouldLogSetting(const QString& displayName)
{
  // don't log Geometry/ and Widgets/, too noisy and not very useful
  static const QStringList ignorePrefixes = {"Geometry/", "Widgets/"};

  for (auto&& prefix : ignorePrefixes) {
    if (displayName.startsWith(prefix, Qt::CaseInsensitive)) {
      return false;
    }
  }

  return true;
}

void logRemoval(const QString& name)
{
  if (!shouldLogSetting(name)) {
    return;
  }

  log::debug("setting '{}' removed", name);
}

QString settingName(const QString& section, const QString& key)
{
  if (section.isEmpty()) {
    return key;
  } else if (key.isEmpty()) {
    return section;
  } else {
    if (section.compare("General", Qt::CaseInsensitive) == 0) {
      return key;
    } else {
      return section + "/" + key;
    }
  }
}

void removeImpl(QSettings& settings, const QString& displayName, const QString& section,
                const QString& key)
{
  if (key.isEmpty()) {
    if (!settings.childGroups().contains(section, Qt::CaseInsensitive)) {
      // not there
      return;
    }
  } else {
    if (!settings.contains(settingName(section, key))) {
      // not there
      return;
    }
  }

  logRemoval(displayName);
  settings.remove(settingName(section, key));
}

void remove(QSettings& settings, const QString& section, const QString& key)
{
  removeImpl(settings, settingName(section, key), section, key);
}

void removeSection(QSettings& settings, const QString& section)
{
  removeImpl(settings, section, section, "");
}

ScopedGroup::ScopedGroup(QSettings& s, const QString& name)
    : m_settings(s), m_name(name)
{
  m_settings.beginGroup(m_name);
}

ScopedGroup::~ScopedGroup()
{
  m_settings.endGroup();
}

void ScopedGroup::remove(const QString& key)
{
  removeImpl(m_settings, settingName(m_name, key), "", key);
}

QStringList ScopedGroup::keys() const
{
  return m_settings.childKeys();
}

ScopedReadArray::ScopedReadArray(QSettings& s, const QString& section)
    : m_settings(s), m_count(0)
{
  m_count = m_settings.beginReadArray(section);
}

ScopedReadArray::~ScopedReadArray()
{
  m_settings.endArray();
}

int ScopedReadArray::count() const
{
  return m_count;
}

QStringList ScopedReadArray::keys() const
{
  return m_settings.childKeys();
}

ScopedWriteArray::ScopedWriteArray(QSettings& s, const QString& section,
                                   std::size_t size)
    : m_settings(s), m_section(section), m_i(0)
{
  m_settings.beginWriteArray(section, size == NoSize ? -1 : static_cast<int>(size));
}

ScopedWriteArray::~ScopedWriteArray()
{
  m_settings.endArray();
}

void ScopedWriteArray::next()
{
  m_settings.setArrayIndex(m_i);
  ++m_i;
}

QString widgetNameWithTopLevel(const QWidget* widget)
{
  QStringList components;

  auto* tl = widget->window();

  if (tl == widget) {
    // this is a top level widget, such as a dialog
    components.push_back(widget->objectName());
  } else {
    // this is a widget
    const auto toplevelName = tl->objectName();
    if (!toplevelName.isEmpty()) {
      components.push_back(toplevelName);
    }

    const auto widgetName = widget->objectName();
    if (!widgetName.isEmpty()) {
      components.push_back(widgetName);
    }
  }

  if (components.isEmpty()) {
    // can't do much
    return "unknown_widget";
  }

  return components.join("_");
}

QString widgetName(const QMainWindow* w)
{
  return w->objectName();
}

QString widgetName(const QHeaderView* w)
{
  return widgetNameWithTopLevel(w->parentWidget());
}

QString widgetName(const ExpanderWidget* w)
{
  return widgetNameWithTopLevel(w->button());
}

QString widgetName(const QWidget* w)
{
  return widgetNameWithTopLevel(w);
}

QString dockSettingName(const QDockWidget* dock)
{
  return "MainWindow_docks_" + dock->objectName() + "_size";
}

QString indexSettingName(const QWidget* widget)
{
  return widgetNameWithTopLevel(widget) + "_index";
}

QString checkedSettingName(const QAbstractButton* b)
{
  return widgetNameWithTopLevel(b) + "_checked";
}

void warnIfNotCheckable(const QAbstractButton* b)
{
  if (!b->isCheckable()) {
    log::warn("button '{}' used in the settings as a checkbox or radio button "
              "but is not checkable",
              b->objectName());
  }
}

QString credentialName(const QString& key)
{
  return "ModOrganizer2_" + key;
}
