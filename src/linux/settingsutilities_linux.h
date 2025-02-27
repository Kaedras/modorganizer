#pragma once

#include <QString>

QString getSecret(const QString& key) noexcept(false);
bool setSecret(const QString& key, const QString& data) noexcept(false);