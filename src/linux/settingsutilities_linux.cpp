#include "expanderwidget.h"
#include "settingsutilities.h"
#include <QtDBus/QDBusMessage>
#include <utility.h>

#include "stub.h"

// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Secret.html

// dest: org.freedesktop.portal.Desktop
// path: /org/freedesktop/portal/desktop
// method: org.freedesktop.portal.Secret.RetrieveSecret

using namespace MOBase;

bool deleteSecret(const QString& key)
{
  STUB();
  return true;
}

bool addSecret(const QString& key, const QString& data)
{
  STUB();
  return true;
}

QString getSecret(const QString& key)
{
  STUB();
  return {};
}

bool setSecret(const QString& key, const QString& data)
{
  if (data.isEmpty()) {
    return deleteSecret(key);
  } else {
    return addSecret(key, data);
  }
}