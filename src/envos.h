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
  OsInfo();

  // tries to guess whether this process is running in compatibility mode
  //
  bool compatibilityMode() const;

  // whether this process is running as administrator, may be empty if the
  // information is not available
  std::optional<bool> isElevated() const;

  // returns a string with all the above information on one line
  //
  QString toString() const;

private:
  std::optional<bool> m_elevated;
  bool m_compatibilityMode;
  QString m_string;

  // gets whether the process is elevated
  //
  std::optional<bool> getElevated() const;

  // tries to guess whether this process is running in compatibility mode
  //
  bool getCompatibilityMode() const;
};

}  // namespace env

#endif  // ENV_OS_H
