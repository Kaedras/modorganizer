#ifndef OVERLAYFSCONNECTOR_H
#define OVERLAYFSCONNECTOR_H

#include "envdump.h"
#include <QDebug>
#include <QString>
#include <QThread>
#include <exception>
#include <overlayfs/overlayfsmanager.h>
#include <uibase/executableinfo.h>
#include <uibase/filemapping.h>
#include <uibase/log.h>

class OverlayfsConnector;
class OverlayfsConnectorException;

using UsvfsConnector          = OverlayfsConnector;
using UsvfsConnectorException = OverlayfsConnectorException;

class OverlayfsConnectorException : public std::exception
{

public:
  OverlayfsConnectorException(const QString& text) : m_Message(text.toLocal8Bit()) {}

  const char* what() const noexcept override { return m_Message.constData(); }

private:
  QByteArray m_Message;
};

class OverlayfsConnector : public QObject
{

  Q_OBJECT

public:
  OverlayfsConnector();
  ~OverlayfsConnector() override;

  void updateMapping(const MappingType& mapping);

  void updateParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
                    const QString& crashDumpsPath, std::chrono::seconds spawnDelay,
                    QString executableBlacklist, const QStringList& skipFileSuffixes,
                    const QStringList& skipDirectories);

  void updateForcedLibraries(
      const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries);

private:
  OverlayFsManager& m_overlayfsManager;
};

std::vector<pid_t> getRunningOverlayfsProcesses();

#endif  // OVERLAYFSCONNECTOR_H
