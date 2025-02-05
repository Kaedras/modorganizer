#pragma once

#include <QString>

bool setWindowsCredential(const QString& key, const QString& data);
QString getWindowsCredential(const QString& key);