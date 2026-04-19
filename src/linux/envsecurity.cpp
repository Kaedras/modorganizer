#include "../envsecurity.h"
#include "../env.h"

namespace
{
constexpr int SECURITY_PROVIDER_LSM = 1;
}

using namespace MOBase;
using namespace Qt::StringLiterals;
using namespace std;

namespace env
{

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
  QString s;

  if (m_name.isEmpty()) {
    s += u"(no name)"_s;
  } else {
    s += m_name;
  }

  return s;
}

QString SecurityProduct::providerToString() const
{
  QStringList ps;
  if (m_provider == SECURITY_PROVIDER_LSM) {
    ps.push_back(u"lsm"_s);
  }

  return ps.join(u"|"_s);
}

std::vector<SecurityProduct> getSecurityProductsFromWMI()
{
  // no-op
  return {};
}

std::optional<SecurityProduct> getWindowsFirewall()
{
  // no-op
  return {};
}

std::vector<SecurityProduct> getSecurityProducts()
{
  vector<SecurityProduct> v;

  QFile lsm(u"/sys/kernel/security/lsm"_s);
  if (!lsm.open(QIODeviceBase::ReadOnly)) {
    log::warn("Error opening /sys/kernel/security/lsm, {}", lsm.errorString());
    return v;
  }

  QTextStream stream(&lsm);
  QString moduleString = stream.readAll();
  QStringList modules  = moduleString.split(',');
  v.reserve(modules.size());

  for (const auto& module : modules) {
    v.emplace_back(QUuid(), module, SECURITY_PROVIDER_LSM, true, true);
  }

  return v;
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

FileRights makeFileRights(int)
{
  // no-op
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
    fs.rights.hasExecute   = info.permission(QFile::Permission::ExeUser);
  }
  // if the calling process is in group
  else if (info.groupId() == getgid()) {
    fs.rights.normalRights = info.permission(QFile::Permission::ReadGroup) &&
                             info.permission(QFile::Permission::WriteGroup);
    fs.rights.hasExecute   = info.permission(QFile::Permission::ExeGroup);
  } else {
    fs.rights.normalRights = info.permission(QFile::Permission::ReadOther) &&
                             info.permission(QFile::Permission::WriteOther);
    fs.rights.hasExecute   = info.permission(QFile::Permission::ExeOther);
  }

  return fs;
}
}  // namespace env
