#include "envos.h"

#include <QSysInfo>

using namespace Qt::StringLiterals;

namespace env
{

OsInfo::OsInfo()
    : m_elevated(getElevated()), m_compatibilityMode(getCompatibilityMode()),
      m_string(toString())
{}

bool OsInfo::compatibilityMode() const
{
  return m_compatibilityMode;
}

std::optional<bool> OsInfo::isElevated() const
{
  return m_elevated;
}

QString OsInfo::toString() const
{
  QString elevated = u"?"_s;
  if (m_elevated.has_value()) {
    elevated = *m_elevated ? u"yes"_s : u"no"_s;
  }

  return QSysInfo::prettyProductName() % ", version "_L1 % QSysInfo::kernelVersion() %
         ", architecture "_L1 % QSysInfo::currentCpuArchitecture() % ", elevated: "_L1 %
         elevated;
}

}  // namespace env
