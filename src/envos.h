#ifndef ENV_OS_H
#define ENV_OS_H

#include <QString>
#include <optional>

namespace env
{

// a variety of information on the os
//
class OsInfo
{
public:
  struct Version
  {
    int major = 0, minor = 0, build = 0;

    QString toString() const
    {
      return QString("%1.%2.%3").arg(major).arg(minor).arg(build);
    }

    friend bool operator==(const Version& a, const Version& b)
    {
      return a.major == b.major && a.minor == b.minor && a.build == b.build;
    }

    friend bool operator!=(const Version& a, const Version& b) { return !(a == b); }
  };

  virtual ~OsInfo() = default;

  // tries to guess whether this process is running in compatibility mode
  //
  virtual bool compatibilityMode() const = 0;

  // returns the Windows version, may not correspond to the actual version
  // if the process is running in compatibility mode
  // returns the kernel version on linux
  //
  virtual const Version& reportedVersion() const = 0;

  // tries to guess the real Windows version that's running, can be empty
  // returns the kernel version on linux
  //
  virtual const Version& realVersion() const = 0;

  // various information about the current release
  //
  // virtual const Release& release() const;

  // whether this process is running as administrator, may be empty if the
  // information is not available
  virtual std::optional<bool> isElevated() const = 0;

  // returns a string with all the above information on one line
  //
  virtual QString toString() const = 0;
};

OsInfo* CreateInfo();

}  // namespace env

#endif  // ENV_OS_H
