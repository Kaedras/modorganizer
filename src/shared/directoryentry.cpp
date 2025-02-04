/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "directoryentry.h"
#include "../envfs.h"
#include "fileentry.h"
#include "filesorigin.h"
#include "originconnection.h"
#include "util.h"
#include "os_error.h"
#include <log.h>
#include <utility.h>

namespace MOShared
{

using namespace MOBase;
const int MAXPATH_UNICODE = 32767;

template <class F>
void elapsedImpl(std::chrono::nanoseconds& out, F&& f)
{
  if constexpr (DirectoryStats::EnableInstrumentation) {
    const auto start = std::chrono::high_resolution_clock::now();
    f();
    const auto end = std::chrono::high_resolution_clock::now();
    out += (end - start);
  } else {
    f();
  }
}

// elapsed() is not optimized out when EnableInstrumentation is false even
// though it's equivalent that this macro
#define elapsed(OUT, F) (F)();
// #define elapsed(OUT, F) elapsedImpl(OUT, F);

bool DirCompareByName::operator()(const DirectoryEntry* lhs,
                                  const DirectoryEntry* rhs) const
{
  return lhs->getName().compare(rhs->getName(), Qt::CaseInsensitive) < 0;
}

DirectoryEntry::DirectoryEntry(QString name, DirectoryEntry* parent, int originID)
    : m_OriginConnection(new OriginConnection), m_Name(std::move(name)),
      m_Parent(parent), m_Populated(false), m_TopLevel(true)
{
  m_FileRegister.reset(new FileRegister(m_OriginConnection));
  m_Origins.insert(originID);
}

DirectoryEntry::DirectoryEntry(QString name, DirectoryEntry* parent, int originID,
                               boost::shared_ptr<FileRegister> fileRegister,
                               boost::shared_ptr<OriginConnection> originConnection)
    : m_FileRegister(fileRegister), m_OriginConnection(originConnection),
      m_Name(std::move(name)), m_Parent(parent), m_Populated(false), m_TopLevel(false)
{
  m_Origins.insert(originID);
}

DirectoryEntry::~DirectoryEntry()
{
  clear();
}

void DirectoryEntry::clear()
{
  for (auto itor = m_SubDirectories.rbegin(); itor != m_SubDirectories.rend(); ++itor) {
    delete *itor;
  }

  m_Files.clear();
  m_FilesLookup.clear();
  m_SubDirectories.clear();
  m_SubDirectoriesLookup.clear();
}

void DirectoryEntry::addFromOrigin(const QString& originName,
                                   const QString& directory, int priority,
                                   DirectoryStats& stats)
{
  env::DirectoryWalker walker;
  addFromOrigin(walker, originName, directory, priority, stats);
}

void DirectoryEntry::addFromOrigin(env::DirectoryWalker& walker,
                                   const QString& originName,
                                   const QString& directory, int priority,
                                   DirectoryStats& stats)
{
  FilesOrigin& origin = createOrigin(originName, directory, priority, stats);

  if (!directory.isEmpty()) {
    addFiles(walker, origin, directory, stats);
  }

  m_Populated = true;
}

void DirectoryEntry::addFromList(const QString& originName,
                                 const QString& directory, env::Directory& root,
                                 int priority, DirectoryStats& stats)
{
  stats = {};

  FilesOrigin& origin = createOrigin(originName, directory, priority, stats);
  addDir(origin, root, stats);
}

void DirectoryEntry::addDir(FilesOrigin& origin, env::Directory& d,
                            DirectoryStats& stats)
{
  elapsed(stats.dirTimes, [&] {
    for (auto& sd : d.dirs) {
      auto* sdirEntry = getSubDirectory(sd, true, stats, origin.getID());
      sdirEntry->addDir(origin, sd, stats);
    }
  });

  elapsed(stats.fileTimes, [&] {
    for (auto& f : d.files) {
      insert(f, origin, "", -1, stats);
    }
  });

  m_Populated = true;
}

void DirectoryEntry::addFromAllBSAs(const QString& originName,
                                    const QString& directory, int priority,
                                    const std::vector<QString>& archives,
                                    const std::set<QString>& enabledArchives,
                                    const std::vector<QString>& loadOrder,
                                    DirectoryStats& stats)
{
  for (const auto& archive : archives) {
    QFileInfo archiveFile(archive);
    const QString filename = archiveFile.fileName();

    if (!enabledArchives.contains(filename)) {
      continue;
    }

    int order = -1;

    for (const auto& plugin : loadOrder) {
      if (filename.startsWith(plugin + " - ", Qt::CaseInsensitive) ||
          filename.startsWith(plugin + ".", Qt::CaseInsensitive)) {
        auto itor = std::ranges::find(loadOrder, plugin);
        if (itor != loadOrder.end()) {
          order = std::distance(loadOrder.begin(), itor);
        }
      }
    }

    addFromBSA(originName, directory, archive, priority, order, stats);
  }
}

void DirectoryEntry::addFromBSA(const QString& originName,
                                const QString& directory,
                                const QString& archivePath, int priority,
                                int order, DirectoryStats& stats)
{
  FilesOrigin& origin    = createOrigin(originName, directory, priority, stats);
  QFileInfo info(archivePath);
  const auto archiveName = info.fileName();

  if (containsArchive(archiveName)) {
    return;
  }

  BSA::Archive archive;
  BSA::EErrorCode res = BSA::ERROR_NONE;

  try {
    // read() can return an error, but it can also throw if the file is not a
    // valid bsa
    res = archive.read(archivePath.toStdString().c_str(), false);
  } catch (std::exception& e) {
    log::error("invalid bsa '{}', error {}", archivePath, e.what());
    return;
  }

  if ((res != BSA::ERROR_NONE) && (res != BSA::ERROR_INVALIDHASHES)) {
    log::error("invalid bsa '{}', error {}", archivePath, res);
    return;
  }


  QDateTime dt = info.lastModified();
  if (!info.exists()) {
    log::warn("failed to get last modified date for '{}'", archivePath);
  }

  addFiles(origin, archive.getRoot(), dt, archiveName, order, stats);

  m_Populated = true;
}

void DirectoryEntry::propagateOrigin(int origin)
{
  {
    std::scoped_lock lock(m_OriginsMutex);
    m_Origins.insert(origin);
  }

  if (m_Parent != nullptr) {
    m_Parent->propagateOrigin(origin);
  }
}

bool DirectoryEntry::originExists(const QString& name) const
{
  return m_OriginConnection->exists(name);
}

FilesOrigin& DirectoryEntry::getOriginByID(int ID) const
{
  return m_OriginConnection->getByID(ID);
}

FilesOrigin& DirectoryEntry::getOriginByName(const QString& name) const
{
  return m_OriginConnection->getByName(name);
}

const FilesOrigin* DirectoryEntry::findOriginByID(int ID) const
{
  return m_OriginConnection->findByID(ID);
}

int DirectoryEntry::anyOrigin() const
{
  bool ignore;

  for (auto iter = m_Files.begin(); iter != m_Files.end(); ++iter) {
    FileEntryPtr entry = m_FileRegister->getFile(iter->second);
    if ((entry.get() != nullptr) && !entry->isFromArchive()) {
      return entry->getOrigin(ignore);
    }
  }

  // if we got here, no file directly within this directory is a valid indicator for a
  // mod, thus we continue looking in subdirectories
  for (DirectoryEntry* entry : m_SubDirectories) {
    int res = entry->anyOrigin();
    if (res != InvalidOriginID) {
      return res;
    }
  }

  return *(m_Origins.begin());
}

std::vector<FileEntryPtr> DirectoryEntry::getFiles() const
{
  std::vector<FileEntryPtr> result;
  result.reserve(m_Files.size());

  for (auto iter = m_Files.begin(); iter != m_Files.end(); ++iter) {
    result.push_back(m_FileRegister->getFile(iter->second));
  }

  return result;
}

DirectoryEntry* DirectoryEntry::findSubDirectory(const QString& name,
                                                 bool alreadyLowerCase) const
{
  SubDirectoriesLookup::const_iterator itor;

  if (alreadyLowerCase) {
    itor = m_SubDirectoriesLookup.find(name);
  } else {
    itor = m_SubDirectoriesLookup.find(name.toLower());
  }

  if (itor == m_SubDirectoriesLookup.end()) {
    return nullptr;
  }

  return itor->second;
}

DirectoryEntry* DirectoryEntry::findSubDirectoryRecursive(const QString& path)
{
  DirectoryStats dummy;
  return getSubDirectoryRecursive(path, false, dummy, InvalidOriginID);
}

const FileEntryPtr DirectoryEntry::findFile(const QString& name,
                                            bool alreadyLowerCase) const
{
  FilesLookup::const_iterator iter;

  if (alreadyLowerCase) {
    iter = m_FilesLookup.find(DirectoryEntryFileKey(name));
  } else {
    iter = m_FilesLookup.find(DirectoryEntryFileKey(name.toLower()));
  }

  if (iter != m_FilesLookup.end()) {
    return m_FileRegister->getFile(iter->second);
  } else {
    return FileEntryPtr();
  }
}

const FileEntryPtr DirectoryEntry::findFile(const DirectoryEntryFileKey& key) const
{
  auto iter = m_FilesLookup.find(key);

  if (iter != m_FilesLookup.end()) {
    return m_FileRegister->getFile(iter->second);
  } else {
    return FileEntryPtr();
  }
}

bool DirectoryEntry::hasFile(const QString& name) const
{
  return m_Files.contains(name.toLower());
}

bool DirectoryEntry::containsArchive(QString archiveName)
{
  for (auto iter = m_Files.begin(); iter != m_Files.end(); ++iter) {
    FileEntryPtr entry = m_FileRegister->getFile(iter->second);
    if (entry->isFromArchive(archiveName)) {
      return true;
    }
  }

  return false;
}

const FileEntryPtr DirectoryEntry::searchFile(const QString& path,
                                              const DirectoryEntry** directory) const
{
  if (directory != nullptr) {
    *directory = nullptr;
  }

  if (path.isEmpty() || (path == "*")) {
    // no file name -> the path ended on a (back-)slash
    if (directory != nullptr) {
      *directory = this;
    }

    return FileEntryPtr();
  }

  const size_t len = path.split('/').first().size();

  if (len == path.size()) {
    // no more path components
    auto iter = m_Files.find(path.toLower());

    if (iter != m_Files.end()) {
      return m_FileRegister->getFile(iter->second);
    } else if (directory != nullptr) {
      DirectoryEntry* temp = findSubDirectory(path);
      if (temp != nullptr) {
        *directory = temp;
      }
    }
  } else {
    // file is in a subdirectory, recurse into the matching subdirectory
    QString pathComponent = path.sliced(0, len);
    DirectoryEntry* temp       = findSubDirectory(pathComponent);

    if (temp != nullptr) {
      if (len >= path.size()) {
        log::error("{}", QObject::tr("unexpected end of path"));
        return FileEntryPtr();
      }

      return temp->searchFile(path.sliced(len + 1), directory);
    }
  }

  return FileEntryPtr();
}

void DirectoryEntry::removeFile(FileIndex index)
{
  removeFileFromList(index);
}

bool DirectoryEntry::removeFile(const QString& filePath, int* origin)
{
  size_t pos = filePath.split('/').first().size();

  if (pos == filePath.size()) {
    return this->remove(filePath, origin);
  }

  QString dirName = filePath.sliced(0, pos);
  QString rest    = filePath.sliced(pos + 1);

  DirectoryStats dummy;
  DirectoryEntry* entry = getSubDirectoryRecursive(dirName, false, dummy);

  if (entry != nullptr) {
    return entry->removeFile(rest, origin);
  } else {
    return false;
  }
}

void DirectoryEntry::removeDir(const QString& path)
{
  size_t pos = path.split('/').first().size();

  if (pos == path.size()) {
    for (auto iter = m_SubDirectories.begin(); iter != m_SubDirectories.end(); ++iter) {
      DirectoryEntry* entry = *iter;

      if (entry->getName().compare(path, Qt::CaseInsensitive) == 0) {
        entry->removeDirRecursive();
        removeDirectoryFromList(iter);
        delete entry;
        break;
      }
    }
  } else {
    QString dirName = path.sliced(0, pos);
    QString rest    = path.sliced(pos + 1);

    DirectoryStats dummy;
    DirectoryEntry* entry = getSubDirectoryRecursive(dirName, false, dummy);

    if (entry != nullptr) {
      entry->removeDir(rest);
    }
  }
}

bool DirectoryEntry::remove(const QString& fileName, int* origin)
{
  const auto lcFileName = fileName.toLower();

  auto iter = m_Files.find(lcFileName);
  bool b    = false;

  if (iter != m_Files.end()) {
    if (origin != nullptr) {
      FileEntryPtr entry = m_FileRegister->getFile(iter->second);
      if (entry.get() != nullptr) {
        bool ignore;
        *origin = entry->getOrigin(ignore);
      }
    }

    b = m_FileRegister->removeFile(iter->second);
  }

  return b;
}

bool DirectoryEntry::hasContentsFromOrigin(int originID) const
{
  return m_Origins.find(originID) != m_Origins.end();
}

FilesOrigin& DirectoryEntry::createOrigin(const QString& originName,
                                          const QString& directory, int priority,
                                          DirectoryStats& stats)
{
  auto r = m_OriginConnection->getOrCreate(originName, directory, priority,
                                           m_FileRegister, m_OriginConnection, stats);

  if (r.second) {
    ++stats.originCreate;
  } else {
    ++stats.originExists;
  }

  return r.first;
}

void DirectoryEntry::removeFiles(const std::set<FileIndex>& indices)
{
  removeFilesFromList(indices);
}

FileEntryPtr DirectoryEntry::insert(const QString& fileName, FilesOrigin& origin,
                                    QDateTime fileTime, const QString& archive,
                                    int order, DirectoryStats& stats)
{
  QString fileNameLower = fileName.toLower();
  FileEntryPtr fe;

  DirectoryEntryFileKey key(std::move(fileNameLower));

  {
    std::unique_lock lock(m_FilesMutex);

    FilesLookup::iterator itor;

    elapsed(stats.filesLookupTimes, [&] {
      itor = m_FilesLookup.find(key);
    });

    if (itor != m_FilesLookup.end()) {
      lock.unlock();
      ++stats.fileExists;
      fe = m_FileRegister->getFile(itor->second);
    } else {
      ++stats.fileCreate;
      fe = m_FileRegister->createFile(fileName,
                                      this, stats);

      elapsed(stats.addFileTimes, [&] {
        addFileToList(std::move(key.value), fe->getIndex());
      });

      // fileNameLower has moved from this point
    }
  }

  elapsed(stats.addOriginToFileTimes, [&] {
    fe->addOrigin(origin.getID(), fileTime, archive, order);
  });

  elapsed(stats.addFileToOriginTimes, [&] {
    origin.addFile(fe->getIndex());
  });

  return fe;
}

FileEntryPtr DirectoryEntry::insert(env::File& file, FilesOrigin& origin,
                                    const QString& archive, int order,
                                    DirectoryStats& stats)
{
  FileEntryPtr fe;

  {
    std::unique_lock lock(m_FilesMutex);

    FilesMap::iterator itor;

    elapsed(stats.filesLookupTimes, [&] {
      itor = m_Files.find(file.lcname);
    });

    if (itor != m_Files.end()) {
      lock.unlock();
      ++stats.fileExists;
      fe = m_FileRegister->getFile(itor->second);
    } else {
      ++stats.fileCreate;
      fe = m_FileRegister->createFile(std::move(file.name), this, stats);
      // file.name has been moved from this point

      elapsed(stats.addFileTimes, [&] {
        addFileToList(std::move(file.lcname), fe->getIndex());
      });

      // file.lcname has been moved from this point
    }
  }

  elapsed(stats.addOriginToFileTimes, [&] {
    fe->addOrigin(origin.getID(), file.lastModified, archive, order);
  });

  elapsed(stats.addFileToOriginTimes, [&] {
    origin.addFile(fe->getIndex());
  });

  return fe;
}

struct DirectoryEntry::Context
{
  FilesOrigin& origin;
  DirectoryStats& stats;
  std::stack<DirectoryEntry*> current;
};

void DirectoryEntry::addFiles(env::DirectoryWalker& walker, FilesOrigin& origin,
                              const QString& path, DirectoryStats& stats)
{
  Context cx = {origin, stats};
  cx.current.push(this);

  walker.forEachEntry(
      path, &cx,
      [](void* pcx, const QString& path) {
        onDirectoryStart((Context*)pcx, path);
      },

      [](void* pcx, const QString& path) {
        onDirectoryEnd((Context*)pcx, path);
      },

      [](void* pcx, const QString& path, QDateTime ft, uint64_t) {
        onFile((Context*)pcx, path, ft);
      });
}

void DirectoryEntry::onDirectoryStart(Context* cx, const QString& path)
{
  elapsed(cx->stats.dirTimes, [&] {
    auto* sd =
        cx->current.top()->getSubDirectory(path, true, cx->stats, cx->origin.getID());

    cx->current.push(sd);
  });
}

void DirectoryEntry::onDirectoryEnd(Context* cx, const QString& path)
{
  elapsed(cx->stats.dirTimes, [&] {
    cx->current.pop();
  });
}

void DirectoryEntry::onFile(Context* cx, const QString& path, QDateTime ft)
{
  elapsed(cx->stats.fileTimes, [&] {
    cx->current.top()->insert(path, cx->origin, ft, "", -1, cx->stats);
  });
}

void DirectoryEntry::addFiles(FilesOrigin& origin, const BSA::Folder::Ptr archiveFolder,
                              QDateTime fileTime, const QString& archiveName,
                              int order, DirectoryStats& stats)
{
  // add files
  const auto fileCount = archiveFolder->getNumFiles();
  for (unsigned int i = 0; i < fileCount; ++i) {
    const BSA::File::Ptr file = archiveFolder->getFile(i);

    auto f = insert(QString::fromStdString(file->getName()), origin, fileTime, archiveName,
                    order, stats);

    if (f) {
      if (file->getUncompressedFileSize() > 0) {
        f->setFileSize(file->getFileSize(), file->getUncompressedFileSize());
      } else {
        f->setFileSize(file->getFileSize(), FileEntry::NoFileSize);
      }
    }
  }

  // recurse into subdirectories
  const auto dirCount = archiveFolder->getNumSubFolders();
  for (unsigned int i = 0; i < dirCount; ++i) {
    const BSA::Folder::Ptr folder = archiveFolder->getSubFolder(i);

    DirectoryEntry* folderEntry = getSubDirectoryRecursive(
        QString::fromStdString(folder->getName()), true, stats, origin.getID());

    folderEntry->addFiles(origin, folder, fileTime, archiveName, order, stats);
  }
}

DirectoryEntry* DirectoryEntry::getSubDirectory(const QString& name, bool create,
                                                DirectoryStats& stats, int originID)
{
  QString nameLc = name.toLower();

  std::scoped_lock lock(m_SubDirMutex);

  SubDirectoriesLookup::iterator itor;
  elapsed(stats.subdirLookupTimes, [&] {
    itor = m_SubDirectoriesLookup.find(nameLc);
  });

  if (itor != m_SubDirectoriesLookup.end()) {
    ++stats.subdirExists;
    return itor->second;
  }

  if (create) {
    ++stats.subdirCreate;

    auto* entry = new DirectoryEntry(name, this,
                                     originID, m_FileRegister, m_OriginConnection);

    elapsed(stats.addDirectoryTimes, [&] {
      addDirectoryToList(entry, std::move(nameLc));
      // nameLc is moved from this point
    });

    return entry;
  } else {
    return nullptr;
  }
}

DirectoryEntry* DirectoryEntry::getSubDirectory(env::Directory& dir, bool create,
                                                DirectoryStats& stats, int originID)
{
  SubDirectoriesLookup::iterator itor;

  std::scoped_lock lock(m_SubDirMutex);

  elapsed(stats.subdirLookupTimes, [&] {
    itor = m_SubDirectoriesLookup.find(dir.lcname);
  });

  if (itor != m_SubDirectoriesLookup.end()) {
    ++stats.subdirExists;
    return itor->second;
  }

  if (create) {
    ++stats.subdirCreate;

    auto* entry = new DirectoryEntry(std::move(dir.name), this, originID,
                                     m_FileRegister, m_OriginConnection);
    // dir.name is moved from this point

    elapsed(stats.addDirectoryTimes, [&] {
      addDirectoryToList(entry, std::move(dir.lcname));
    });

    // dir.lcname is moved from this point

    return entry;
  } else {
    return nullptr;
  }
}

DirectoryEntry* DirectoryEntry::getSubDirectoryRecursive(const QString& path,
                                                         bool create,
                                                         DirectoryStats& stats,
                                                         int originID)
{
  if (path.length() == 0) {
    // path ended with a backslash?
    return this;
  }

  const size_t pos = path.split('/').first().size();

  if (pos == path.size()) {
    return getSubDirectory(path, create, stats);
  } else {
    DirectoryEntry* nextChild =
        getSubDirectory(path.sliced(0, pos), create, stats, originID);

    if (nextChild == nullptr) {
      return nullptr;
    } else {
      return nextChild->getSubDirectoryRecursive(path.sliced(pos + 1), create, stats,
                                                 originID);
    }
  }
}

void DirectoryEntry::removeDirRecursive()
{
  while (!m_Files.empty()) {
    m_FileRegister->removeFile(m_Files.begin()->second);
  }

  m_FilesLookup.clear();

  for (DirectoryEntry* entry : m_SubDirectories) {
    entry->removeDirRecursive();
    delete entry;
  }

  m_SubDirectories.clear();
  m_SubDirectoriesLookup.clear();
}

void DirectoryEntry::addDirectoryToList(DirectoryEntry* e, QString nameLc)
{
  m_SubDirectories.insert(e);
  m_SubDirectoriesLookup.emplace(std::move(nameLc), e);
}

void DirectoryEntry::removeDirectoryFromList(SubDirectories::iterator itor)
{
  const auto* entry = *itor;

  {
    auto itor2 = std::find_if(m_SubDirectoriesLookup.begin(),
                              m_SubDirectoriesLookup.end(), [&](auto&& d) {
                                return (d.second == entry);
                              });

    if (itor2 == m_SubDirectoriesLookup.end()) {
      log::error("entry {} not in sub directories map", entry->getName());
    } else {
      m_SubDirectoriesLookup.erase(itor2);
    }
  }

  m_SubDirectories.erase(itor);
}

void DirectoryEntry::removeFileFromList(FileIndex index)
{
  auto removeFrom = [&](auto& list) {
    auto iter = std::find_if(list.begin(), list.end(), [&index](auto&& pair) {
      return (pair.second == index);
    });

    if (iter == list.end()) {
      auto f = m_FileRegister->getFile(index);

      if (f) {
        log::error("can't remove file '{}', not in directory entry '{}'", f->getName(),
                   getName());
      } else {
        log::error("can't remove file with index {}, not in directory entry '{}' and "
                   "not in register",
                   index, getName());
      }
    } else {
      list.erase(iter);
    }
  };

  removeFrom(m_FilesLookup);
  removeFrom(m_Files);
}

void DirectoryEntry::removeFilesFromList(const std::set<FileIndex>& indices)
{
  for (auto iter = m_Files.begin(); iter != m_Files.end();) {
    if (indices.find(iter->second) != indices.end()) {
      iter = m_Files.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = m_FilesLookup.begin(); iter != m_FilesLookup.end();) {
    if (indices.find(iter->second) != indices.end()) {
      iter = m_FilesLookup.erase(iter);
    } else {
      ++iter;
    }
  }
}

void DirectoryEntry::addFileToList(QString fileNameLower, FileIndex index)
{
  m_FilesLookup.emplace(fileNameLower, index);
  m_Files.emplace(std::move(fileNameLower), index);
  // fileNameLower has been moved from this point
}

struct DumpFailed : public std::runtime_error
{
  using runtime_error::runtime_error;
};

void DirectoryEntry::dump(const QString& file) const
{
  try {
    std::FILE* f = nullptr;
    auto e       = _wfopen_s(&f, file.c_str(), L"wb");

    if (e != 0 || !f) {
      throw DumpFailed(std::format("failed to open, {} ({})", std::strerror(e), e));
    }

    Guard g([&] {
      std::fclose(f);
    });

    dump(f, "Data");
  } catch (DumpFailed& e) {
    log::error("failed to write list to '{}': {}",
               file.toStdString(), e.what());
  }
}

void DirectoryEntry::dump(std::FILE* f, const QString& parentPath) const
{
  {
    std::scoped_lock lock(m_FilesMutex);

    for (auto&& index : m_Files) {
      const auto file = m_FileRegister->getFile(index.second);
      if (!file) {
        continue;
      }

      if (file->isFromArchive()) {
        // TODO: don't list files from archives. maybe make this an option?
        continue;
      }

      const auto& o   = m_OriginConnection->getByID(file->getOrigin());
      const auto path = parentPath + "/" + file->getName();
      const auto line = path + "\t(" + o.getName() + ")\r\n";

      const auto lineu8 = line.toStdString();

      if (std::fwrite(lineu8.data(), lineu8.size(), 1, f) != 1) {
        const auto e = errno;
        throw DumpFailed(std::format("failed to write, {} ({})", std::strerror(e), e));
      }
    }
  }

  {
    std::scoped_lock lock(m_SubDirMutex);
    for (auto&& d : m_SubDirectories) {
      const auto path = parentPath + "/" + d->m_Name;
      d->dump(f, path);
    }
  }
}

}  // namespace MOShared
