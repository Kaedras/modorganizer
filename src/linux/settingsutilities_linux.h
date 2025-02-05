#pragma once

#include <QString>

QString getSecret(const QString& key);
bool setSecret(const QString& key, const QString& data);