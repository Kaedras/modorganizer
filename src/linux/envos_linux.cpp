#include "../envos.h"
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
    QString name = u"Linux"_s;
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
  QString m_versionString;
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
  sl << m_release.name;

  if (!m_release.version.isEmpty()) {
    sl << m_release.version;
  }

  // version
  sl << u"Kernel "_s % m_versionString;  // kernel release including local version,
                                         // e.g. 6.9.9-gentoo
  sl << m_info.machine;                  // architecture, e.g. x86_64
  sl << m_info.version;                  // kernel version, e.g. #1 SMP PREEMPT_DYNAMIC

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
  // documentation of os-release can be found here:
  // https://www.freedesktop.org/software/systemd/man/latest/os-release.html

  auto parseRelease = [](const QString& fileName) -> Release {
    Release r;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
      log::warn("Error opening file {}, {}", fileName, file.errorString());
      return r;
    }

    QTextStream stream(&file);
    QString line;
    while (stream.readLineInto(&line)) {
      if (line.startsWith("PRETTY_NAME"_L1)) {
        r.name = line.trimmed();
      }

      // both VERSION and VERSION_ID are optional fields
      // use VERSION if present
      if (line.startsWith("VERSION"_L1)) {
        r.version = line.trimmed();
      }
      // use VERSION_ID instead
      else if (line.startsWith("VERSION_ID"_L1) && r.version.isEmpty()) {
        r.version = line.trimmed();
      }
    }

    return r;
  };

  // check /etc/os-release
  if (QFile::exists(u"/etc/os-release"_s)) {
    return parseRelease(u"/etc/os-release"_s);
  }

  // check /usr/lib/os-release
  if (QFile::exists(u"/usr/lib/os-release"_s)) {
    return parseRelease(u"/usr/lib/os-release"_s);
  }

  return {};
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
    // remove local version string
    for (int i = 0; i < list[2].length(); i++) {
      if (!list[2][i].isDigit()) {
        list[2].truncate(i);
        break;
      }
    }

    m_version.major = list[0].toInt();
    m_version.minor = list[1].toInt();
    m_version.build = list[2].toInt();

    m_versionString = versionString;
  }
}

std::unique_ptr<OsInfo> CreateInfo()
{
  return std::make_unique<LinuxInfo>();
}

}  // namespace env
