#include "../envsecurity.h"
#include "../env.h"
#include "stub.h"
#include <linux/compatibility.h>
#include <log.h>
#include <pwd.h>
#include <sys/types.h>
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
  passwd* p = getpwuid(owner);
  if (p == nullptr) {
    return {};
  }
  return QString::fromLocal8Bit(p->pw_name);
}

FileRights makeFileRights(int m)
{
  (void)m;
  STUB();
  return {};
}

FileSecurity getFileSecurity(const QString& path)
{
  QFileInfo info(path);
  FileSecurity fs;
  fs.owner = info.ownerId() == getuid() ? "(this user)" : info.owner();
  // if the calling process is owner
  if (info.ownerId() == getuid()) {
    fs.rights.normalRights = info.permission(QFile::Permission::ReadUser) &&
                             info.permission(QFile::Permission::WriteUser);
    fs.rights.hasExecute = info.permission(QFile::Permission::ExeUser);
  }
  // if the calling process is in group
  else if (info.groupId() == getgid()) {
    fs.rights.normalRights = info.permission(QFile::Permission::ReadGroup) &&
                             info.permission(QFile::Permission::WriteGroup);
    fs.rights.hasExecute = info.permission(QFile::Permission::ExeGroup);
  } else {
    fs.rights.normalRights = info.permission(QFile::Permission::ReadOther) &&
                             info.permission(QFile::Permission::WriteOther);
    fs.rights.hasExecute = info.permission(QFile::Permission::ExeOther);
  }

  return fs;
}
}  // namespace env
