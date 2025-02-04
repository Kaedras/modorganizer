#ifndef MO_REGISTER_FILEENTRY_INCLUDED
#define MO_REGISTER_FILEENTRY_INCLUDED

#include "fileregisterfwd.h"

namespace MOShared
{

class FileEntry
{
public:
  static constexpr uint64_t NoFileSize = std::numeric_limits<uint64_t>::max();

  FileEntry();
  FileEntry(FileIndex index, const QString& name, DirectoryEntry* parent);

  // noncopyable
  FileEntry(const FileEntry&)            = delete;
  FileEntry& operator=(const FileEntry&) = delete;

  FileIndex getIndex() const { return m_Index; }

  void addOrigin(OriginID origin, QDateTime fileTime, const QString& archive,
                 int order);

  // remove the specified origin from the list of origins that contain this
  // file. if no origin is left, the file is effectively deleted and true is
  // returned. otherwise, false is returned
  bool removeOrigin(OriginID origin);

  void sortOrigins();

  // gets the list of alternative origins (origins with lower priority than
  // the primary one). if sortOrigins has been called, it is sorted by priority
  // (ascending)
  const AlternativesVector& getAlternatives() const { return m_Alternatives; }

  const QString& getName() const { return m_Name; }

  OriginID getOrigin() const { return m_Origin; }

  OriginID getOrigin(bool& archive) const
  {
    archive = m_Archive.isValid();
    return m_Origin;
  }

  const DataArchiveOrigin& getArchive() const { return m_Archive; }

  bool isFromArchive(const QString& archiveName = "") const;

  // if originID is -1, uses the main origin; if this file doesn't exist in the
  // given origin, returns an empty string
  //
  QString getFullPath(OriginID originID = InvalidOriginID) const;

  QString getRelativePath() const;

  DirectoryEntry* getParent() { return m_Parent; }

  void setFileTime(QDateTime fileTime) const { m_FileTime = fileTime; }

  QDateTime getFileTime() const { return m_FileTime; }

  void setFileSize(uint64_t size, uint64_t compressedSize)
  {
    m_FileSize           = size;
    m_CompressedFileSize = compressedSize;
  }

  uint64_t getFileSize() const { return m_FileSize; }

  uint64_t getCompressedFileSize() const { return m_CompressedFileSize; }

private:
  FileIndex m_Index;
  QString m_Name;
  OriginID m_Origin;
  DataArchiveOrigin m_Archive;
  AlternativesVector m_Alternatives;
  DirectoryEntry* m_Parent;
  mutable QDateTime m_FileTime;
  uint64_t m_FileSize, m_CompressedFileSize;
  mutable std::mutex m_OriginsMutex;

  bool recurseParents(QString& path, const DirectoryEntry* parent) const;
};

}  // namespace MOShared

#endif  // MO_REGISTER_FILEENTRY_INCLUDED
