#include "envmodule.h"
#include "env.h"
#include "stub.h"

#include <filesystem>
#include <log.h>
#include <utility.h>

using namespace std;
using namespace Qt::StringLiterals;
namespace fs = std::filesystem;

namespace env
{
using namespace MOBase;

Module::Module(QString path, std::size_t fileSize)
    : m_path(std::move(path)), m_fileSize(fileSize)
{
  m_version   = getVersion();
  m_timestamp = QFileInfo(path).birthTime();
}

const QString& Module::path() const
{
  return m_path;
}

QString Module::displayPath() const
{
  return m_path.toLower();
}

std::size_t Module::fileSize() const
{
  return m_fileSize;
}

const QString& Module::version() const
{
  return m_version;
}

const QDateTime& Module::timestamp() const
{
  return m_timestamp;
}

QString Module::timestampString() const
{
  if (!m_timestamp.isValid()) {
    return "(no timestamp)";
  }

  return m_timestamp.toString(Qt::DateFormat::ISODate);
}

QString Module::toString() const
{
  QStringList sl;

  // file size
  sl.push_back(displayPath());
  sl.push_back(QString("%1 B").arg(m_fileSize));

  // version
  if (m_version.isEmpty()) {
    sl.push_back("(no version)");
  } else {
    if (!m_version.isEmpty()) {
      sl.push_back(m_version);
    }
  }

  // timestamp
  if (m_timestamp.isValid()) {
    sl.push_back(m_timestamp.toString(Qt::DateFormat::ISODate));
  } else {
    sl.push_back("(no timestamp)");
  }

  return sl.join(", ");
}

QString Module::getVersion() const
{
  if (m_path.contains(".so."_L1)) {
    auto pos = m_version.indexOf(".so.");
    return m_version.sliced(pos);
  }
  return {};
}

bool Module::interesting() const
{
  if (m_path.startsWith("/usr/lib"_L1) || m_path.startsWith("/lib"_L1)) {
    return false;
  }

  return true;
}

}  // namespace env
