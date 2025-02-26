#include "modinforegular.h"

#include "categories.h"
#include "messagedialog.h"
#include "moddatacontent.h"
#include "organizercore.h"
#include "plugincontainer.h"
#include "report.h"
#include "settings.h"
#include <iplugingame.h>

#include <QApplication>
#include <QDirIterator>
#include <QSettings>

#include <sstream>

using namespace MOBase;
using namespace MOShared;
using namespace Qt::StringLiterals;

namespace
{
// Arguably this should be a class static or we should be using FileString rather
// than QString for the names. Or both.
static bool ByName(const ModInfo::Ptr& LHS, const ModInfo::Ptr& RHS)
{
  return QString::compare(LHS->name(), RHS->name(), Qt::CaseInsensitive) < 0;
}
}  // namespace

ModInfoRegular::ModInfoRegular(const QDir& path, OrganizerCore& core)
    : ModInfoWithConflictInfo(core), m_Name(path.dirName()),
      m_Path(path.absolutePath()), m_Repository(),
      m_GameName(core.managedGame()->gameShortName()), m_IsAlternate(false),
      m_Converted(false), m_Validated(false), m_MetaInfoChanged(false),
      m_EndorsedState(EndorsedState::ENDORSED_UNKNOWN),
      m_TrackedState(TrackedState::TRACKED_UNKNOWN),
      m_NexusBridge(&core.pluginContainer())
{
  m_CreationTime = QFileInfo(path.absolutePath()).birthTime();
  // read out the meta-file for information
  readMeta();
  if (m_GameName.compare(core.managedGame()->gameShortName(), Qt::CaseInsensitive) != 0)
    if (!core.managedGame()->primarySources().contains(m_GameName, Qt::CaseInsensitive))
      m_IsAlternate = true;

  // populate m_Archives
  m_Archives = QStringList();
  if (Settings::instance().archiveParsing()) {
    archives(true);
  }

  connect(&m_NexusBridge,
          SIGNAL(descriptionAvailable(QString, int, QVariant, QVariant)), this,
          SLOT(nxmDescriptionAvailable(QString, int, QVariant, QVariant)));
  connect(&m_NexusBridge, SIGNAL(endorsementToggled(QString, int, QVariant, QVariant)),
          this, SLOT(nxmEndorsementToggled(QString, int, QVariant, QVariant)));
  connect(&m_NexusBridge, SIGNAL(trackingToggled(QString, int, QVariant, bool)), this,
          SLOT(nxmTrackingToggled(QString, int, QVariant, bool)));
  connect(&m_NexusBridge,
          SIGNAL(requestFailed(QString, int, int, QVariant, int, QString)), this,
          SLOT(nxmRequestFailed(QString, int, int, QVariant, int, QString)));
}

ModInfoRegular::~ModInfoRegular()
{
  try {
    saveMeta();
  } catch (const std::exception& e) {
    log::error("failed to save meta information for \"{}\": {}", m_Name, e.what());
  }
}

bool ModInfoRegular::isEmpty() const
{
  QDirIterator iter(m_Path, QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);
  if (!iter.hasNext())
    return true;
  iter.next();
  if ((iter.fileName() == "meta.ini") && !iter.hasNext())
    return true;
  return false;
}

void ModInfoRegular::readMeta()
{
  QSettings metaFile(m_Path % u"/meta.ini"_s, QSettings::IniFormat);
  m_Comments           = metaFile.value(u"comments"_s, "").toString();
  m_Notes              = metaFile.value(u"notes"_s, "").toString();
  QString tempGameName = metaFile.value(u"gameName"_s, m_GameName).toString();
  if (tempGameName.size())
    m_GameName = tempGameName;
  m_NexusID = metaFile.value(u"modid"_s, -1).toInt();
  m_Version.parse(metaFile.value(u"version"_s, "").toString());
  m_NewestVersion    = metaFile.value(u"newestVersion"_s, "").toString();
  m_IgnoredVersion   = metaFile.value(u"ignoredVersion"_s, "").toString();
  m_InstallationFile = metaFile.value(u"installationFile"_s, "").toString();
  m_NexusDescription = metaFile.value(u"nexusDescription"_s, "").toString();
  m_NexusFileStatus  = metaFile.value(u"nexusFileStatus"_s, "1").toInt();
  m_NexusCategory    = metaFile.value(u"nexusCategory"_s, 0).toInt();
  m_Repository       = metaFile.value(u"repository"_s, u"Nexus"_s).toString();
  m_Converted        = metaFile.value(u"converted"_s, false).toBool();
  m_Validated        = metaFile.value(u"validated"_s, false).toBool();

  // this handles changes to how the URL works after 2.2.0
  //
  // in 2.2.0, "hasCustomUrl" does not exist and "url" is only used when the mod
  // id is invalid, although it can be set at any time in the mod info dialog
  //
  // post 2.2.0, a custom url can be set on any mod, whether the mod id is
  // valid or not, so an additional flag "hasCustomURL" is required, with a
  // corresponding checkbox in the mod info dialog
  //
  // there are several cases to handle to make sure no data is lost and to
  // determine whether the user has set a custom url before:
  //
  //   1) some mods have an incorrect url set along with a valid mod id;
  //      there is apparently a bug with the fomod installer that can set the
  //      url of a mod to a value used by a _previous_ installation
  //
  //   2) it is possible to set the url even if the mod id is valid, in which
  //      case it is saved, but never used in 2.2.0
  //
  //   3) opening the mod info dialog on the nexus tab for a mod that has a
  //      valid id will force the url to be the same as what the plugin gives
  //      back
  //
  // the algorithm is as follows:
  //   always read the url from the meta file and store it so this piece of data
  //   is never lost; the problem then only becomes about whether to enable
  //   hasCustomURL
  //
  //   if hasCustomURL is present in the meta file, just read that and be
  //   done with it
  //
  //   if not, then the flag depends on the mod id and the url
  //      if the mod id is valid, the custom url is disabled; although the url
  //      could be _set_ by the user when a mod id was valid, it was never
  //      _used_, so the behaviour won't change
  //
  //      if the mod id is invalid, the url should normally be empty, unless the
  //      user specified one, in which case hasCustomURL should be true
  //        (the only case where this fails is if a mod id was valid before and
  //        the user visited the nexus tab, in which case the url was set
  //        automatically, but then the id was manually changed to 0
  //
  //        in that case, the mod id is invalid and the url is not empty, but it
  //        was never set by the user; this case is impossible to distinguish
  //        from a user manually entering a url, and so is handled as such)

  // always read the url
  m_CustomURL = metaFile.value(u"url"_s).toString();

  if (metaFile.contains(u"hasCustomURL"_s)) {
    m_HasCustomURL = metaFile.value(u"hasCustomURL"_s).toBool();
  } else {
    if (m_NexusID > 0) {
      // the mod id is valid, disable the custom url
      m_HasCustomURL = false;
    } else {
      if (!m_CustomURL.isEmpty()) {
        // the mod id is invalid and the url is not empty, enable it
        m_HasCustomURL = true;
      }
    }
  }

  m_LastNexusQuery = QDateTime::fromString(
      metaFile.value(u"lastNexusQuery"_s, "").toString(), Qt::ISODate);
  m_LastNexusUpdate = QDateTime::fromString(
      metaFile.value(u"lastNexusUpdate"_s, "").toString(), Qt::ISODate);
  m_NexusLastModified = QDateTime::fromString(
      metaFile.value(u"nexusLastModified"_s, QDateTime::currentDateTimeUtc()).toString(),
      Qt::ISODate);
  m_NexusCategory = metaFile.value(u"nexusCategory"_s, 0).toInt();
  m_Color         = metaFile.value(u"color"_s, QColor()).value<QColor>();
  m_TrackedState  = metaFile.value(u"tracked"_s, false).toBool()
                        ? TrackedState::TRACKED_TRUE
                        : TrackedState::TRACKED_FALSE;
  if (metaFile.contains(u"endorsed"_s)) {
    if (metaFile.value(u"endorsed"_s).canConvert<int>()) {
      using ut = std::underlying_type_t<EndorsedState>;
      switch (metaFile.value(u"endorsed"_s).toInt()) {
      case static_cast<ut>(EndorsedState::ENDORSED_FALSE):
        m_EndorsedState = EndorsedState::ENDORSED_FALSE;
        break;
      case static_cast<ut>(EndorsedState::ENDORSED_TRUE):
        m_EndorsedState = EndorsedState::ENDORSED_TRUE;
        break;
      case static_cast<ut>(EndorsedState::ENDORSED_NEVER):
        m_EndorsedState = EndorsedState::ENDORSED_NEVER;
        break;
      default:
        m_EndorsedState = EndorsedState::ENDORSED_UNKNOWN;
        break;
      }
    } else {
      m_EndorsedState = metaFile.value(u"endorsed"_s, false).toBool()
                            ? EndorsedState::ENDORSED_TRUE
                            : EndorsedState::ENDORSED_FALSE;
    }
  }

  QString categoriesString = metaFile.value(u"category"_s, "").toString();

  QStringList categories = categoriesString.split(',', Qt::SkipEmptyParts);
  for (QStringList::iterator iter = categories.begin(); iter != categories.end();
       ++iter) {
    bool ok        = false;
    int categoryID = iter->toInt(&ok);
    if (categoryID < 0) {
      // ignore invalid id
      continue;
    }
    if (ok && (categoryID != 0) &&
        (CategoryFactory::instance().categoryExists(categoryID))) {
      m_Categories.insert(categoryID);
      if (iter == categories.begin()) {
        m_PrimaryCategory = categoryID;
      }
    }
  }

  int numFiles = metaFile.beginReadArray(u"installedFiles"_s);
  for (int i = 0; i < numFiles; ++i) {
    metaFile.setArrayIndex(i);
    m_InstalledFileIDs.insert(std::make_pair(metaFile.value(u"modid"_s).toInt(),
                                             metaFile.value(u"fileid"_s).toInt()));
  }
  metaFile.endArray();

  // Plugin settings:
  metaFile.beginGroup(u"Plugins");
  for (auto pluginName : metaFile.childGroups()) {
    metaFile.beginGroup(pluginName);
    for (auto settingKey : metaFile.childKeys()) {
      m_PluginSettings[pluginName][settingKey] = metaFile.value(settingKey);
    }
    metaFile.endGroup();
  }
  metaFile.endGroup();

  m_MetaInfoChanged = false;
}

void ModInfoRegular::saveMeta()
{
  // only write meta data if the mod directory exists
  if (m_MetaInfoChanged && QFile::exists(absolutePath())) {
    QSettings metaFile(absolutePath().append(u"/meta.ini"_s), QSettings::IniFormat);
    if (metaFile.status() == QSettings::NoError) {
      std::set<int> temp = m_Categories;
      temp.erase(m_PrimaryCategory);
      metaFile.setValue(u"category"_s, QStringLiteral(u"%1,%2").arg(m_PrimaryCategory).arg(SetJoin(temp, u","_s)));
      metaFile.setValue(u"newestVersion"_s, m_NewestVersion.canonicalString());
      metaFile.setValue(u"ignoredVersion"_s, m_IgnoredVersion.canonicalString());
      metaFile.setValue(u"version"_s, m_Version.canonicalString());
      metaFile.setValue(u"installationFile"_s, m_InstallationFile);
      metaFile.setValue(u"repository"_s, m_Repository);
      metaFile.setValue(u"gameName"_s, m_GameName);
      metaFile.setValue(u"modid"_s, m_NexusID);
      metaFile.setValue(u"comments"_s, m_Comments);
      metaFile.setValue(u"notes"_s, m_Notes);
      metaFile.setValue(u"nexusDescription"_s, m_NexusDescription);
      metaFile.setValue(u"url"_s, m_CustomURL);
      metaFile.setValue(u"hasCustomURL"_s, m_HasCustomURL);
      metaFile.setValue(u"nexusFileStatus"_s, m_NexusFileStatus);
      metaFile.setValue(u"lastNexusQuery"_s, m_LastNexusQuery.toString(Qt::ISODate));
      metaFile.setValue(u"lastNexusUpdate"_s, m_LastNexusUpdate.toString(Qt::ISODate));
      metaFile.setValue(u"nexusLastModified"_s, m_NexusLastModified.toString(Qt::ISODate));
      metaFile.setValue(u"nexusCategory"_s, m_NexusCategory);
      metaFile.setValue(u"converted"_s, m_Converted);
      metaFile.setValue(u"validated"_s, m_Validated);
      metaFile.setValue(u"color"_s, m_Color);
      if (m_EndorsedState != EndorsedState::ENDORSED_UNKNOWN) {
        metaFile.setValue(
            u"endorsed"_s,
            static_cast<std::underlying_type_t<EndorsedState>>(m_EndorsedState));
      }
      if (m_TrackedState != TrackedState::TRACKED_UNKNOWN) {
        metaFile.setValue(u"tracked"_s, static_cast<std::underlying_type_t<TrackedState>>(
                                         m_TrackedState));
      }

      metaFile.remove(u"installedFiles");
      metaFile.beginWriteArray(u"installedFiles");
      int idx = 0;
      for (auto iter = m_InstalledFileIDs.begin(); iter != m_InstalledFileIDs.end();
           ++iter) {
        metaFile.setArrayIndex(idx++);
        metaFile.setValue(u"modid"_s, iter->first);
        metaFile.setValue(u"fileid"_s, iter->second);
      }
      metaFile.endArray();

      // Plugin settings:
      metaFile.remove(u"Plugins"_s);
      metaFile.beginGroup(u"Plugins"_s);
      for (const auto& [pluginName, pluginSettings] : m_PluginSettings) {
        metaFile.beginGroup(pluginName);
        for (const auto& [settingName, settingValue] : pluginSettings) {
          metaFile.setValue(settingName, settingValue);
        }
        metaFile.endGroup();
      }
      metaFile.endGroup();

      metaFile.sync();  // sync needs to be called to ensure the file is created

      if (metaFile.status() == QSettings::NoError) {
        m_MetaInfoChanged = false;
      } else {
        log::error("failed to write {}/meta.ini: error {}", absolutePath(),
                   metaFile.status());
      }
    } else {
      log::error("failed to write {}/meta.ini: error {}", absolutePath(),
                 metaFile.status());
    }
  }
}

bool ModInfoRegular::updateAvailable() const
{
  if (m_IgnoredVersion.isValid() && (m_IgnoredVersion == m_NewestVersion)) {
    return false;
  }
  if (m_NexusFileStatus == 4 || m_NexusFileStatus == 6) {
    return true;
  }
  return m_NewestVersion.isValid() && (m_Version < m_NewestVersion);
}

bool ModInfoRegular::downgradeAvailable() const
{
  if (m_IgnoredVersion.isValid() && (m_IgnoredVersion == m_NewestVersion)) {
    return false;
  }
  return m_NewestVersion.isValid() && (m_NewestVersion < m_Version);
}

void ModInfoRegular::nxmDescriptionAvailable(QString, int, QVariant,
                                             QVariant resultData)
{
  QVariantMap result = resultData.toMap();
  setNexusDescription(result["description"].toString());

  if ((m_EndorsedState != EndorsedState::ENDORSED_NEVER) &&
      (result.contains(u"endorsement"_s))) {
    QVariantMap endorsement   = result[u"endorsement"_s].toMap();
    QString endorsementStatus = endorsement[u"endorse_status"_s].toString();
    if (endorsementStatus.compare(u"Endorsed"_s, Qt::CaseInsensitive) == 00)
      setEndorsedState(EndorsedState::ENDORSED_TRUE);
    else if (endorsementStatus.compare(u"Abstained"_s, Qt::CaseInsensitive) == 00)
      setEndorsedState(EndorsedState::ENDORSED_NEVER);
    else
      setEndorsedState(EndorsedState::ENDORSED_FALSE);
  }
  m_LastNexusQuery = QDateTime::currentDateTimeUtc();
  m_NexusLastModified =
      QDateTime::fromSecsSinceEpoch(result[u"updated_timestamp"_s].toInt(), Qt::UTC);
  m_MetaInfoChanged = true;
  saveMeta();
  disconnect(sender(), SIGNAL(descriptionAvailable(QString, int, QVariant, QVariant)));
  emit modDetailsUpdated(true);
}

void ModInfoRegular::nxmEndorsementToggled(QString, int, QVariant, QVariant resultData)
{
  QMap results = resultData.toMap();
  QMutexLocker locker(&s_Mutex);
  for (auto& mod : s_Collection) {
    if (mod->gameName().compare(m_GameName, Qt::CaseInsensitive) == 0 &&
        mod->nexusId() == m_NexusID) {
      if (results[u"status"_s].toString().compare(u"Endorsed"_s) == 0) {
        mod->setIsEndorsed(true);
      } else if (results[u"status"_s].toString().compare(u"Abstained"_s) == 0) {
        mod->setNeverEndorse();
      } else {
        mod->setIsEndorsed(false);
      }
      mod->saveMeta();
    }
  }
  emit modDetailsUpdated(true);
}

void ModInfoRegular::nxmTrackingToggled(QString, int, QVariant, bool tracked)
{
  QMutexLocker locker(&s_Mutex);
  for (auto& mod : s_Collection) {
    if (mod->gameName().compare(m_GameName, Qt::CaseInsensitive) == 0 &&
        mod->nexusId() == m_NexusID) {
      mod->setIsTracked(tracked);
      mod->saveMeta();
    }
  }
  emit modDetailsUpdated(true);
}

void ModInfoRegular::nxmRequestFailed(QString, int, int, QVariant userData,
                                      int errorCode, const QString& errorMessage)
{
  QString fullMessage = errorMessage;
  if (userData.canConvert<int>() && (userData.toInt() == 1)) {
    fullMessage += "\nNexus will reject endorsements within 15 Minutes of a failed "
                   "attempt, the error message may be misleading.";
  }
  if (QApplication::activeWindow() != nullptr) {
    MessageDialog::showMessage(fullMessage, QApplication::activeWindow());
  }
  emit modDetailsUpdated(false);
}

bool ModInfoRegular::updateNXMInfo()
{
  if (needsDescriptionUpdate()) {
    m_NexusBridge.requestDescription(m_GameName, m_NexusID, QVariant());
    return true;
  }

  return false;
}

bool ModInfoRegular::needsDescriptionUpdate() const
{
  if (m_NexusID > 0) {
    QDateTime time   = QDateTime::currentDateTimeUtc();
    QDateTime target = m_LastNexusQuery.addDays(1);

    if (time >= target) {
      return true;
    }
  }

  return false;
}

void ModInfoRegular::setCategory(int categoryID, bool active)
{
  m_MetaInfoChanged = true;

  if (active) {
    m_Categories.insert(categoryID);
    if (m_PrimaryCategory == -1) {
      m_PrimaryCategory = categoryID;
    }
  } else {
    std::set<int>::iterator iter = m_Categories.find(categoryID);
    if (iter != m_Categories.end()) {
      m_Categories.erase(iter);
    }
    if (categoryID == m_PrimaryCategory) {
      if (m_Categories.size() == 0) {
        m_PrimaryCategory = -1;
      } else {
        m_PrimaryCategory = *(m_Categories.begin());
      }
    }
  }
}

bool ModInfoRegular::setName(const QString& name)
{
  if (name.contains('/') || name.contains('\\')) {
    return false;
  }

  QString newPath =
      m_Path.mid(0).replace(m_Path.length() - m_Name.length(), m_Name.length(), name);
  QDir modDir(m_Path.mid(0, m_Path.length() - m_Name.length()));

  if (m_Name.compare(name, Qt::CaseInsensitive) == 0) {
    QString tempName = name;
    tempName.append(u"_temp"_s);
    while (modDir.exists(tempName)) {
      tempName.append(u"_"_s);
    }
    if (!modDir.rename(m_Name, tempName)) {
      return false;
    }
    if (!modDir.rename(tempName, name)) {
      log::error(
          "rename to final name failed after successful rename to intermediate name");
      modDir.rename(tempName, m_Name);
      return false;
    }
  } else {
    auto result =
        shellRename(modDir.absoluteFilePath(m_Name), modDir.absoluteFilePath(name));
    if (!result) {
      log::error("failed to rename mod {} (errorcode {})", name, result.error);
      return false;
    }
  }

  std::map<QString, unsigned int>::iterator nameIter = s_ModsByName.find(m_Name);
  if (nameIter != s_ModsByName.end()) {
    QMutexLocker locker(&s_Mutex);

    unsigned int index = nameIter->second;
    s_ModsByName.erase(nameIter);

    m_Name = name;
    m_Path = newPath;

    s_ModsByName[m_Name] = index;

    std::sort(s_Collection.begin(), s_Collection.end(), ModInfo::ByName);
    updateIndices();
  } else {  // otherwise mod isn't registered yet?
    m_Name = name;
    m_Path = newPath;
  }

  return true;
}

void ModInfoRegular::setComments(const QString& comments)
{
  m_Comments        = comments;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setNotes(const QString& notes)
{
  m_Notes           = notes;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setGameName(const QString& gameName)
{
  m_GameName        = gameName;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setNexusID(int modID)
{
  m_NexusID         = modID;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setVersion(const VersionInfo& version)
{
  m_Version         = version;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setNewestVersion(const VersionInfo& version)
{
  if (version != m_NewestVersion) {
    m_NewestVersion   = version;
    m_MetaInfoChanged = true;
  }
}

void ModInfoRegular::setNexusDescription(const QString& description)
{
  if (qHash(description) != qHash(m_NexusDescription)) {
    m_NexusDescription = description;
    m_MetaInfoChanged  = true;
  }
}

void ModInfoRegular::setEndorsedState(EndorsedState endorsedState)
{
  if (endorsedState != m_EndorsedState) {
    m_EndorsedState   = endorsedState;
    m_MetaInfoChanged = true;
  }
}

void ModInfoRegular::setTrackedState(TrackedState trackedState)
{
  if (trackedState != m_TrackedState) {
    m_TrackedState    = trackedState;
    m_MetaInfoChanged = true;
  }
}

void ModInfoRegular::setInstallationFile(const QString& fileName)
{
  m_InstallationFile = fileName;
  m_MetaInfoChanged  = true;
}

void ModInfoRegular::addNexusCategory(int categoryID)
{
  m_Categories.insert(CategoryFactory::instance().resolveNexusID(categoryID));
}

void ModInfoRegular::setIsEndorsed(bool endorsed)
{
  m_EndorsedState =
      endorsed ? EndorsedState::ENDORSED_TRUE : EndorsedState::ENDORSED_FALSE;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setNeverEndorse()
{
  m_EndorsedState   = EndorsedState::ENDORSED_NEVER;
  m_MetaInfoChanged = true;
}

void ModInfoRegular::setIsTracked(bool tracked)
{
  if (tracked != (m_TrackedState == TrackedState::TRACKED_TRUE)) {
    m_TrackedState = tracked ? TrackedState::TRACKED_TRUE : TrackedState::TRACKED_FALSE;
    m_MetaInfoChanged = true;
  }
}

void ModInfoRegular::setColor(QColor color)
{
  m_Color           = color;
  m_MetaInfoChanged = true;
}

QColor ModInfoRegular::color() const
{
  return m_Color;
}

void ModInfoRegular::endorse(bool doEndorse)
{
  if (doEndorse != (m_EndorsedState == EndorsedState::ENDORSED_TRUE)) {
    m_NexusBridge.requestToggleEndorsement(
        m_GameName, nexusId(), m_Version.canonicalString(), doEndorse, QVariant(1));
  }
}

void ModInfoRegular::track(bool doTrack)
{
  if (doTrack != (m_TrackedState == TrackedState::TRACKED_TRUE)) {
    m_NexusBridge.requestToggleTracking(m_GameName, nexusId(), doTrack, QVariant(1));
  }
}

void ModInfoRegular::markConverted(bool converted)
{
  m_Converted       = converted;
  m_MetaInfoChanged = true;
  saveMeta();
  emit modDetailsUpdated(true);
}

void ModInfoRegular::markValidated(bool validated)
{
  m_Validated       = validated;
  m_MetaInfoChanged = true;
  saveMeta();
  emit modDetailsUpdated(true);
}

QString ModInfoRegular::absolutePath() const
{
  return m_Path;
}

void ModInfoRegular::ignoreUpdate(bool ignore)
{
  if (ignore) {
    m_IgnoredVersion = m_NewestVersion;
  } else {
    m_IgnoredVersion.clear();
  }
  m_MetaInfoChanged = true;
}

bool ModInfoRegular::canBeUpdated() const
{
  QDateTime now    = QDateTime::currentDateTimeUtc();
  QDateTime target = getExpires();
  if (now >= target)
    return m_NexusID > 0;
  return false;
}

QDateTime ModInfoRegular::getExpires() const
{
  return m_LastNexusUpdate.addSecs(300);
}

std::vector<ModInfo::EFlag> ModInfoRegular::getFlags() const
{
  std::vector<ModInfo::EFlag> result = ModInfoWithConflictInfo::getFlags();
  if ((m_NexusID > 0) && (endorsedState() == EndorsedState::ENDORSED_FALSE) &&
      Settings::instance().nexus().endorsementIntegration()) {
    result.push_back(ModInfo::FLAG_NOTENDORSED);
  }
  if ((m_NexusID > 0) && (trackedState() == TrackedState::TRACKED_TRUE) &&
      Settings::instance().nexus().trackedIntegration()) {
    result.push_back(ModInfo::FLAG_TRACKED);
  }
  if (!isValid() && !m_Validated) {
    result.push_back(ModInfo::FLAG_INVALID);
  }
  if (m_Notes.length() != 0) {
    result.push_back(ModInfo::FLAG_NOTES);
  }
  if (m_PluginSelected) {
    result.push_back(ModInfo::FLAG_PLUGIN_SELECTED);
  }
  if (m_IsAlternate && !m_Converted) {
    result.push_back(ModInfo::FLAG_ALTERNATE_GAME);
  }
  return result;
}

std::set<int> ModInfoRegular::doGetContents() const
{
  auto contentFeature =
      m_Core.pluginContainer().gameFeatures().gameFeature<ModDataContent>();

  if (contentFeature) {
    auto result = contentFeature->getContentsFor(fileTree());
    return std::set<int>(std::begin(result), std::end(result));
  }

  return {};
}

int ModInfoRegular::getHighlight() const
{
  if (!isValid() && !m_Validated)
    return HIGHLIGHT_INVALID;
  auto flags = getFlags();
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_PLUGIN_SELECTED) !=
      flags.end())
    return HIGHLIGHT_PLUGIN;
  return HIGHLIGHT_NONE;
}

QString ModInfoRegular::getDescription() const
{
  if (!isValid() && !m_Validated) {
    return tr("%1 contains no esp/esm/esl and no asset (textures, meshes, interface, "
              "...) directory")
        .arg(name());
  } else {
    const std::set<int>& categories = getCategories();
    std::wostringstream categoryString;
    categoryString << ToWString(tr("Categories: <br>"));
    CategoryFactory& categoryFactory = CategoryFactory::instance();
    for (std::set<int>::const_iterator catIter = categories.begin();
         catIter != categories.end(); ++catIter) {
      if (catIter != categories.begin()) {
        categoryString << " , ";
      }
      categoryString << "<span style=\"white-space: nowrap;\"><i>"
                     << ToWString(categoryFactory.getCategoryName(
                            categoryFactory.getCategoryIndex(*catIter)))
                     << "</font></span>";
    }

    return ToQString(categoryString.str());
  }
}

int ModInfoRegular::getNexusFileStatus() const
{
  return m_NexusFileStatus;
}

void ModInfoRegular::setNexusFileStatus(int status)
{
  m_NexusFileStatus = status;
  m_MetaInfoChanged = true;
  saveMeta();
  emit modDetailsUpdated(true);
}

QString ModInfoRegular::comments() const
{
  return m_Comments;
}

QString ModInfoRegular::notes() const
{
  return m_Notes;
}

QDateTime ModInfoRegular::creationTime() const
{
  return m_CreationTime;
}

QString ModInfoRegular::getNexusDescription() const
{
  return m_NexusDescription;
}

QString ModInfoRegular::repository() const
{
  return m_Repository;
}

EndorsedState ModInfoRegular::endorsedState() const
{
  return m_EndorsedState;
}

TrackedState ModInfoRegular::trackedState() const
{
  return m_TrackedState;
}

QDateTime ModInfoRegular::getLastNexusUpdate() const
{
  return m_LastNexusUpdate;
}

void ModInfoRegular::setLastNexusUpdate(QDateTime time)
{
  m_LastNexusUpdate = time;
  m_MetaInfoChanged = true;
  saveMeta();
  emit modDetailsUpdated(true);
}

QDateTime ModInfoRegular::getLastNexusQuery() const
{
  return m_LastNexusQuery;
}

void ModInfoRegular::setLastNexusQuery(QDateTime time)
{
  m_LastNexusQuery  = time;
  m_MetaInfoChanged = true;
  saveMeta();
  emit modDetailsUpdated(true);
}

QDateTime ModInfoRegular::getNexusLastModified() const
{
  return m_NexusLastModified;
}

void ModInfoRegular::setNexusLastModified(QDateTime time)
{
  m_NexusLastModified = time;
  m_MetaInfoChanged   = true;
  saveMeta();
  emit modDetailsUpdated(true);
}

int ModInfoRegular::getNexusCategory() const
{
  return m_NexusCategory;
}

void ModInfoRegular::setNexusCategory(int category)
{
  m_NexusCategory   = category;
  m_MetaInfoChanged = true;
  saveMeta();
}

void ModInfoRegular::setCustomURL(QString const& url)
{
  m_CustomURL       = url;
  m_MetaInfoChanged = true;
}

QString ModInfoRegular::url() const
{
  return m_CustomURL;
}

void ModInfoRegular::setHasCustomURL(bool b)
{
  m_HasCustomURL    = b;
  m_MetaInfoChanged = true;
}

bool ModInfoRegular::hasCustomURL() const
{
  return m_HasCustomURL;
}

QStringList ModInfoRegular::archives(bool checkOnDisk)
{
  if (checkOnDisk) {
    QStringList result;
    QDir dir(this->absolutePath());
    QStringList bsaList = dir.entryList(QStringList({u"*.bsa"_s, u"*.ba2"_s}));
    for (const QString& archive : bsaList) {
      result.append(this->absolutePath() % u"/"_s % archive);
    }
    m_Archives = result;
  }
  return m_Archives;
}

void ModInfoRegular::addInstalledFile(int modId, int fileId)
{
  m_InstalledFileIDs.insert(std::make_pair(modId, fileId));
  m_MetaInfoChanged = true;
}

std::vector<QString> ModInfoRegular::getIniTweaks() const
{
  QString metaFileName = absolutePath().append(u"/meta.ini"_s);
  QSettings metaFile(metaFileName, QSettings::IniFormat);

  std::vector<QString> result;

  int numTweaks = metaFile.beginReadArray(u"INI Tweaks"_s);

  if (numTweaks != 0) {
    log::debug("{} active ini tweaks in {}", numTweaks,
               QDir::toNativeSeparators(metaFileName));
  }

  for (int i = 0; i < numTweaks; ++i) {
    metaFile.setArrayIndex(i);
    QString filename =
        absolutePath() % u"/INI Tweaks/"_s % metaFile.value(u"name"_s).toString();
    result.push_back(filename);
  }
  metaFile.endArray();
  return result;
}

std::map<QString, QVariant>
ModInfoRegular::pluginSettings(const QString& pluginName) const
{
  auto itp = m_PluginSettings.find(pluginName);
  if (itp == std::end(m_PluginSettings)) {
    return {};
  }
  return itp->second;
}

QVariant ModInfoRegular::pluginSetting(const QString& pluginName, const QString& key,
                                       const QVariant& defaultValue) const
{
  auto itp = m_PluginSettings.find(pluginName);
  if (itp == std::end(m_PluginSettings)) {
    return defaultValue;
  }

  auto its = itp->second.find(key);
  if (its == std::end(itp->second)) {
    return defaultValue;
  }

  return its->second;
}

bool ModInfoRegular::setPluginSetting(const QString& pluginName, const QString& key,
                                      const QVariant& value)
{
  m_PluginSettings[pluginName][key] = value;
  m_MetaInfoChanged                 = true;
  saveMeta();
  return true;
}

std::map<QString, QVariant>
ModInfoRegular::clearPluginSettings(const QString& pluginName)
{
  auto itp = m_PluginSettings.find(pluginName);
  if (itp == std::end(m_PluginSettings)) {
    return {};
  }
  auto settings = itp->second;
  m_PluginSettings.erase(itp);
  saveMeta();
  return settings;
}
