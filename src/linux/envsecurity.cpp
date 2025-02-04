#include "envsecurity.h"
#include "compatibility.h"
#include "env.h"
#include "envmodule.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

SecurityProduct::SecurityProduct(QUuid guid, QString name, int provider, bool active,
                                 bool upToDate)
    : m_guid(std::move(guid)), m_name(std::move(name)), m_provider(provider),
      m_active(active), m_upToDate(upToDate)
{}

const QUuid& SecurityProduct::guid() const
{
  return m_guid;
}

const QString& SecurityProduct::name() const
{
  return m_name;
}

int SecurityProduct::provider() const
{
  return m_provider;
}

bool SecurityProduct::active() const
{
  return m_active;
}

bool SecurityProduct::upToDate() const
{
  return m_upToDate;
}

QString SecurityProduct::toString() const
{
  STUB();
  return {};
}

QString SecurityProduct::providerToString() const
{
  STUB();
  return {};
}

std::vector<SecurityProduct> getSecurityProductsFromWMI()
{
  STUB();
  return {};
}

std::optional<SecurityProduct> getWindowsFirewall()
{
  STUB();
  return {};
}

std::vector<SecurityProduct> getSecurityProducts()
{
  STUB();
  return {};
}

class failed
{
public:
  failed(DWORD e, QString what)
      : m_what(what + ", " + QString::fromStdString(formatSystemMessage(e)))
  {}

  QString what() const { return m_what; }

private:
  QString m_what;
};


QString getUsername(int owner)
{
  STUB();
  return {};
}

FileRights makeFileRights(int m)
{
  STUB();
  return {};
}

FileSecurity getFileSecurity(const QString& path)
{
  STUB();
  return {};
}
}  // namespace env
