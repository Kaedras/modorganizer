#include "expanderwidget.h"
#include "settingsutilities.h"
#include <utility.h>

using namespace MOBase;

extern QString secretName(const QString& key);

bool deleteWindowsCredential(const QString& key)
{
  const auto credName = secretName(key);

  if (!CredDeleteW(credName.toStdWString().c_str(), CRED_TYPE_GENERIC, 0)) {
    const auto e = GetLastError();

    // not an error if the key already doesn't exist, and don't log it because
    // it happens all the time when the settings dialog is closed since it
    // doesn't check first
    if (e == ERROR_NOT_FOUND) {
      return true;
    }

    log::error("failed to delete windows credential {}, {}", credName,
               formatSystemMessage(e));
    return false;
  }

  log::debug("deleted windows credential {}", credName);

  return true;
}

bool addWindowsCredential(const QString& key, const QString& data)
{
  const auto credName = secretName(key);

  const auto wname = credName.toStdWString();
  const auto wdata = data.toStdWString();

  const auto* blob    = reinterpret_cast<const BYTE*>(wdata.data());
  const auto blobSize = wdata.size() * sizeof(decltype(wdata)::value_type);

  CREDENTIALW cred        = {};
  cred.Flags              = 0;
  cred.Type               = CRED_TYPE_GENERIC;
  cred.TargetName         = const_cast<wchar_t*>(wname.c_str());
  cred.CredentialBlob     = const_cast<BYTE*>(blob);
  cred.CredentialBlobSize = static_cast<DWORD>(blobSize);
  cred.Persist            = CRED_PERSIST_LOCAL_MACHINE;

  if (!CredWriteW(&cred, 0)) {
    const auto e = GetLastError();

    log::error("failed to delete windows credential {}, {}", credName,
               formatSystemMessage(e));

    return false;
  }

  log::debug("set windows credential {}", credName);

  return true;
}

struct CredentialFreer
{
  void operator()(CREDENTIALW* c)
  {
    if (c) {
      CredFree(c);
    }
  }
};

using CredentialPtr = std::unique_ptr<CREDENTIALW, CredentialFreer>;

QString getSecret(const QString& key)
{
  const QString credName = secretName(key);

  CREDENTIALW* rawCreds = nullptr;

  const auto ret =
      CredReadW(credName.toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &rawCreds);

  CredentialPtr creds(rawCreds);

  if (!ret) {
    const auto e = GetLastError();

    if (e != ERROR_NOT_FOUND) {
      log::error("failed to retrieve windows credential {}: {}", credName,
                 formatSystemMessage(e));
    }

    return {};
  }

  QString value;
  if (creds->CredentialBlob) {
    value =
        QString::fromWCharArray(reinterpret_cast<const wchar_t*>(creds->CredentialBlob),
                                creds->CredentialBlobSize / sizeof(wchar_t));
  }

  return value;
}

bool setSecret(const QString& key, const QString& data)
{
  if (data.isEmpty()) {
    return deleteWindowsCredential(key);
  } else {
    return addWindowsCredential(key, data);
  }
}
