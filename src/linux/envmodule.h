#ifndef ENV_MODULE_H
#define ENV_MODULE_H

#include <QDateTime>
#include <QString>

namespace env
{
// represents one module
//
class Module
{
public:
  Module(QString path, std::size_t fileSize);

  // returns the module's path
  //
  const QString& path() const;

  // returns the module's path in lowercase
  //
  QString displayPath() const;

  // returns the size in bytes, may be 0
  //
  std::size_t fileSize() const;

  // returns the version from the version name, may be empty
  //
  const QString& version() const;

  // returns the creation time of the file on the filesystem, may be empty
  //
  const QDateTime& timestamp() const;

  // converts timestamp() to a string for display, returns "(no timestamp)" if
  // not available
  //
  QString timestampString() const;

  // returns false for modules in "/lib", "/lib64", "/usr/lib", or "/usr/lib64"
  //
  bool interesting() const;

  // returns a string with all the above information on one line
  //
  QString toString() const;

private:
  QString m_path;
  std::size_t m_fileSize;
  QString m_version;
  QDateTime m_timestamp;

  QString getVersion() const;
};

}  // namespace env
#endif
