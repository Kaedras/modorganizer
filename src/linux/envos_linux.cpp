#include "envos.h"
#include "stub.h"
#include <sys/utsname.h>

#include <log.h>

using namespace MOBase;

namespace env
{

class LinuxInfo final : public OsInfo
{
public:
  struct Release
  {
    // distro name
    QString name;
    // distro version
    QString version;
  };

  const Version& reportedVersion() const override;
  const Version& realVersion() const override;
  std::optional<bool> isElevated() const override;
  QString toString() const override;
  LinuxInfo();
  bool compatibilityMode() const override;

private:
  utsname m_info{};
  Release m_release;
  Version m_version;
  std::optional<bool> m_elevated;

  std::optional<bool> getElevated() const;
  Release getRelease() const;
  void getVersion();
};

const OsInfo::Version& LinuxInfo::reportedVersion() const
{
  return m_version;
}

const OsInfo::Version& LinuxInfo::realVersion() const
{
  return m_version;
}

std::optional<bool> LinuxInfo::isElevated() const
{
  return m_elevated;
}

QString LinuxInfo::toString() const
{
  QStringList sl;

  // distro
  if (!m_release.name.isEmpty()) {
    sl.push_back(m_release.name);
  } else {
    sl.push_back("Unknown linux distribution");
  }
  if (!m_release.version.isEmpty()) {
    sl.push_back(m_release.version);
  }

  // version
  sl.push_back("Kernel " + m_version.toString());  // kernel release, e.g. 6.9.9
  sl.push_back(m_info.machine);                    // architecture, e.g. x86_64
  sl.push_back(m_info.version);  // kernel version, e.g. #1 SMP PREEMPT_DYNAMIC

  // elevated
  QString elevated = "?";
  if (m_elevated.has_value()) {
    elevated = m_elevated.value() ? "yes" : "no";
  }

  sl.push_back("elevated: " + elevated);

  return sl.join(", ");
}

LinuxInfo::LinuxInfo()
{
  m_release = getRelease();
  getVersion();
  m_elevated = getElevated();
}

bool LinuxInfo::compatibilityMode() const
{
  return false;
}

std::optional<bool> LinuxInfo::getElevated() const
{
  return getuid() == 0;
}

LinuxInfo::Release LinuxInfo::getRelease() const
{
  Release r;
  QFile lsbRelease("/etc/lsb-release");
  if (!lsbRelease.open(QIODeviceBase::ReadOnly | QIODeviceBase::Text)) {
    log::error("error opening /etc/lsb-release: '{}'", lsbRelease.errorString());
  } else {
    while (!lsbRelease.atEnd()) {
      QString line = lsbRelease.readLine();

      if (line.startsWith("PRETTY_NAME=")) {
        line.remove(0, strlen("PRETTY_NAME="));
        // remove "
        if (line.startsWith('"')) {
          line.removeFirst();
        }
        // remove trailing "
        if (line.endsWith('"')) {
          line.removeLast();
        }
        r.name = line;
      } else if (line.startsWith("VERSION_ID=")) {
        line.remove(0, strlen("VERSION_ID="));
        // remove "
        if (line.startsWith('"')) {
          line.removeFirst();
        }
        // remove trailing "
        if (line.endsWith('"')) {
          line.removeLast();
        }

        r.version = line;
      }
    }
  }
  return r;
}

void LinuxInfo::getVersion()
{
  if (uname(&m_info) != 0) {
    log::error("error getting kernel version: {}", errno);
  }
  QString versionString = m_info.release;
  QStringList list      = versionString.split('.');
  if (list.size() != 3) {
    log::error("invalid version string size, got '{}'", versionString.toStdString());
  } else {
    m_version.major = list[0].toInt();
    m_version.minor = list[1].toInt();
    m_version.build = list[2].toInt();
  }
}

OsInfo* CreateInfo()
{
  return new LinuxInfo();
}

}  // namespace env
