/*
Copyright (C) 2015 Sebastian Herbord. All rights reserved.

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

#ifndef OVERLAYFSCONNECTOR_H
#define OVERLAYFSCONNECTOR_H

#include "envdump.h"
#include <QDebug>
#include <QFile>
#include <QList>
#include <QString>
#include <QThread>
#include <exception>
#include <overlayfs/OverlayfsManager.h>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>

class OverlayfsConnector;
class OverlayfsConnectorException;

using UsvfsConnector          = OverlayfsConnector;
using UsvfsConnectorException = OverlayfsConnectorException;

// TODO: this class currently contains mostly placeholders

class OverlayfsConnectorException : public std::exception
{

public:
  OverlayfsConnectorException(const QString& text)
      : std::exception(), m_Message(text.toLocal8Bit())
  {}

  virtual const char* what() const throw() { return m_Message.constData(); }

private:
  QByteArray m_Message;
};

class OverlayfsConnector : public QObject
{

  Q_OBJECT

public:
  OverlayfsConnector();
  ~OverlayfsConnector();

  void updateMapping(const MappingType& mapping);

  void updateParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
                    const QString& crashDumpsPath, std::chrono::seconds spawnDelay,
                    QString executableBlacklist, const QStringList& skipFileSuffixes,
                    const QStringList& skipDirectories);

  void updateForcedLibraries(
      const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries);

private:
  OverlayfsManager& m_overlayfsManager;
};

std::vector<pid_t> getRunningOverlayfsProcesses();

#endif  // OVERLAYFSCONNECTOR_H
