#include "envos.h"
#include <sys/utsname.h>

#include <log.h>

using namespace MOBase;
using namespace Qt::StringLiterals;

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
    sl << m_release.name;
  } else {
    sl << u"Unknown linux distribution"_s;
  }
  if (!m_release.version.isEmpty()) {
    sl << m_release.version;
  }

  // version
  sl << u"Kernel "_s % m_version.toString();  // kernel release, e.g. 6.9.9
  sl << m_info.machine;                       // architecture, e.g. x86_64
  sl << m_info.version;  // kernel version, e.g. #1 SMP PREEMPT_DYNAMIC

  // elevated
  QString elevated = u"?"_s;
  if (m_elevated.has_value()) {
    elevated = m_elevated.value() ? u"yes"_s : u"no"_s;
  }

  sl << u"elevated: "_s % elevated;

  return sl.join(u", "_s);
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

  QProcess p;
  p.startCommand(u"lsb_release -d"_s);
  p.waitForFinished();

  if (p.exitCode() != 0) {
    return r;
  }

  QString description = p.readAll();
  description.remove(u"Description:\t"_s);
  r.name = description.trimmed();

  p.startCommand(u"lsb_release -r"_s);
  p.waitForFinished();

  if (p.exitCode() == 0) {
    QString version = p.readAll();
    version.remove(u"Release:\t"_s);
    r.version = version.trimmed();
  }

  return r;
}

void LinuxInfo::getVersion()
{
  if (uname(&m_info) != 0) {
    const int error = errno;
    log::error("error getting kernel version: {}", strerror(error));
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
