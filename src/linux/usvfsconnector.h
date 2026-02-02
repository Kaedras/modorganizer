#ifndef USVFSCONNECTOR_H
#define USVFSCONNECTOR_H

#include "envdump.h"
#include <QString>
#include <exception>
#include <executableinfo.h>
#include <filemapping.h>
#include <log.h>

class UsvfsManager;

class UsvfsConnectorException : public std::exception
{

public:
  UsvfsConnectorException(const QString& text) : m_Message(text.toLocal8Bit()) {}

  const char* what() const noexcept override { return m_Message.constData(); }

private:
  QByteArray m_Message;
};

class UsvfsConnector : public QObject
{

  Q_OBJECT

public:
  UsvfsConnector();
  ~UsvfsConnector() override;

  void updateMapping(const MappingType& mapping);

  void updateParams(MOBase::log::Levels logLevel, env::CoreDumpTypes coreDumpType,
                    const QString& crashDumpsPath, std::chrono::seconds spawnDelay,
                    QString executableBlacklist, const QStringList& skipFileSuffixes,
                    const QStringList& skipDirectories);

  void updateForcedLibraries(
      const QList<MOBase::ExecutableForcedLoadSetting>& forcedLibraries);

private:
  std::shared_ptr<UsvfsManager> m_usvfsManager;
};

std::vector<pid_t> getRunningUSVFSProcesses();

#endif  // USVFSCONNECTOR_H
