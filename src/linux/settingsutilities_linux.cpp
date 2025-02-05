#include "settingsutilities.h"
#include "expanderwidget.h"
#include <utility.h>

#include "stub.h"

using namespace MOBase;

bool deleteSecret(const QString& key){
  STUB();
  return true;
}

bool addSecret(const QString& key, const QString& data){
  STUB();
  return true;
}

QString getSecret(const QString& key){
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