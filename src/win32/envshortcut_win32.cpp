#include "env.h"
#include "envshortcut.h"
#include "executableslist.h"
#include "filesystemutilities.h"
#include "instancemanager.h"
#include <log.h>
#include <utility.h>

namespace env
{

using namespace MOBase;

class ShellLinkException
{
public:
  ShellLinkException(QString s) : m_what(std::move(s)) {}

  const QString& what() const { return m_what; }

private:
  QString m_what;
};

// just a wrapper around IShellLink operations that throws ShellLinkException
// on errors
//
class ShellLinkWrapper
{
public:
  ShellLinkWrapper()
  {
    m_link = createShellLink();
    m_file = createPersistFile();
  }

  void setPath(const QString& s)
  {
    if (s.isEmpty()) {
      throw ShellLinkException("path cannot be empty");
    }

    const auto r = m_link->SetPath(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set target path '%1'").arg(s));
  }

  void setArguments(const QString& s)
  {
    const auto r = m_link->SetArguments(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set arguments '%1'").arg(s));
  }

  void setDescription(const QString& s)
  {
    if (s.isEmpty()) {
      return;
    }

    const auto r = m_link->SetDescription(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set description '%1'").arg(s));
  }

  void setIcon(const QString& file, int i)
  {
    if (file.isEmpty()) {
      return;
    }

    const auto r = m_link->SetIconLocation(file.toStdWString().c_str(), i);
    throwOnFail(r, QString("failed to set icon '%1' @ %2").arg(file).arg(i));
  }

  void setWorkingDirectory(const QString& s)
  {
    if (s.isEmpty()) {
      return;
    }

    const auto r = m_link->SetWorkingDirectory(s.toStdWString().c_str());
    throwOnFail(r, QString("failed to set working directory '%1'").arg(s));
  }

  void save(const QString& path)
  {
    const auto r = m_file->Save(path.toStdWString().c_str(), TRUE);
    throwOnFail(r, QString("failed to save link '%1'").arg(path));
  }

private:
  COMPtr<IShellLink> m_link;
  COMPtr<IPersistFile> m_file;

  void throwOnFail(HRESULT r, const QString& s)
  {
    if (FAILED(r)) {
      throw ShellLinkException(QString("%1, %2").arg(s).arg(formatSystemMessage(r)));
    }
  }

  COMPtr<IShellLink> createShellLink()
  {
    void* link = nullptr;

    const auto r = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IShellLink, &link);

    throwOnFail(r, "failed to create IShellLink instance");

    if (!link) {
      throw ShellLinkException("creating IShellLink worked, pointer is null");
    }

    return COMPtr<IShellLink>(static_cast<IShellLink*>(link));
  }

  COMPtr<IPersistFile> createPersistFile()
  {
    void* file = nullptr;

    const auto r = m_link->QueryInterface(IID_IPersistFile, &file);
    throwOnFail(r, "failed to get IPersistFile interface");

    if (!file) {
      throw ShellLinkException("querying IPersistFile worked, pointer is null");
    }

    return COMPtr<IPersistFile>(static_cast<IPersistFile*>(file));
  }
};

bool Shortcut::add(Locations loc)
{
  log::debug("adding shortcut to {}:\n"
             "  . name: '{}'\n"
             "  . target: '{}'\n"
             "  . arguments: '{}'\n"
             "  . description: '{}'\n"
             "  . icon: '{}' @ {}\n"
             "  . working directory: '{}'",
             toString(loc), m_name, m_target, m_arguments, m_description, m_icon,
             m_iconIndex, m_workingDirectory);

  if (m_target.isEmpty()) {
    log::error("shortcut: target is empty");
    return false;
  }

  const auto path = shortcutPath(loc);
  if (path.isEmpty()) {
    return false;
  }

  log::debug("shorcut file will be saved at '{}'", path);

  try {
    ShellLinkWrapper link;

    link.setPath(m_target);
    link.setArguments(m_arguments);
    link.setDescription(m_description);
    link.setIcon(m_icon, m_iconIndex);
    link.setWorkingDirectory(m_workingDirectory);

    link.save(path);

    return true;
  } catch (ShellLinkException& e) {
    log::error("{}\nshortcut file was not saved", e.what());
  }

  return false;
}

}  // namespace env
