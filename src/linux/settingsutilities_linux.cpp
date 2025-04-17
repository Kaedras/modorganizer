#include "settingsutilities.h"
#include <memory>
#include <utility.h>

// undefine signals from qtmetamacros.h because it conflicts with glib
#ifdef signals
#undef signals
#endif
#include <libsecret/secret.h>

using namespace MOBase;

struct HashTableDeleter
{
  void operator()(GHashTable* h)
  {
    if (h) {
      g_hash_table_destroy(h);
    }
  }
};
using GHashTablePtr = std::unique_ptr<GHashTable, HashTableDeleter>;

struct GListDeleter
{
  void operator()(GList* l)
  {
    if (l) {
      g_list_free(l);
    }
  }
};
using GListPtr = std::unique_ptr<GList, GListDeleter>;

struct SecretValueDeleter
{
  void operator()(SecretValue* v)
  {
    if (v) {
      secret_value_unref(v);
    }
  }
};
using SecretValuePtr = std::unique_ptr<SecretValue, SecretValueDeleter>;
struct SecretServiceDeleter
{
  void operator()(SecretService* s)
  {
    if (s) {
      g_object_unref(s);
    }
  }
};
using SecretServicePtr = std::unique_ptr<SecretService, SecretServiceDeleter>;

// todo: check if using a schema would be beneficial
/*
const SecretSchema *
example_get_schema (void)
{
  static const SecretSchema schema = {
    "org.ModOrganizer2.Password", SECRET_SCHEMA_NONE,
    {
              {  "number", SECRET_SCHEMA_ATTRIBUTE_INTEGER },
              {  "string", SECRET_SCHEMA_ATTRIBUTE_STRING },
              {  "even", SECRET_SCHEMA_ATTRIBUTE_BOOLEAN },
              {nullptr, (SecretSchemaAttributeType)0 },
          }
  };
  return &schema;
}
*/

bool deleteSecret(const QString& key)
{
  GError* e        = nullptr;
  QByteArray keyBA = key.toLocal8Bit();

  GHashTablePtr attributes(g_hash_table_new(nullptr, nullptr));
  g_hash_table_insert(attributes.get(), gpointer("key"), keyBA.data());

  GListPtr items(secret_service_search_sync(nullptr, nullptr, attributes.get(),
                                            SECRET_SEARCH_UNLOCK, nullptr, &e));

  if (e) {
    std::string message = e->message;
    g_error_free(e);

    log::error("failed to delete secret {}, {}", key, message);
    throw std::runtime_error(message);
  }

  // not an error if the key already doesn't exist, and don't log it because
  // it happens all the time when the settings dialog is closed since it
  // doesn't check first
  if (!items) {
    return true;
  }

  guint length = g_list_length(items.get());
  if (length == 1) {
    auto* item = static_cast<SecretItem*>(items.get()[0].data);
    secret_item_delete_sync(item, nullptr, &e);
    if (e) {
      std::string message = e->message;
      g_error_free(e);

      log::error("failed to delete secret {}, {}", key, message);
      throw std::runtime_error(message);
    }
  } else {
    std::string message =
        std::format("failed to delete secret {}, found {} items", key, length);
    log::error(message);
    throw std::runtime_error(message);
  }

  log::debug("deleted secret {}", key);

  return true;
}

bool addSecret(const QString& key, const QString& data)
{
  GError* e = nullptr;

  // connect to service
  SecretServicePtr service(
      secret_service_get_sync(SECRET_SERVICE_OPEN_SESSION, nullptr, &e));
  if (e) {
    std::string message = e->message;
    g_error_free(e);

    log::error("failed to connect to secret service, {}", message);
    throw std::runtime_error(message);
  }

  // try to unlock
  secret_service_unlock_sync(service.get(), nullptr, nullptr, nullptr, &e);
  if (e) {
    std::string message = e->message;
    g_error_free(e);

    log::error("failed to unlock default collection, {}", message);
    throw std::runtime_error(message);
  }

  QByteArray keyBA  = key.toLocal8Bit();
  QByteArray dataBA = data.toLocal8Bit();
  SecretValuePtr secretValue(
      secret_value_new(dataBA.data(), data.size(), "text/plain"));

  GHashTablePtr attributes(g_hash_table_new(nullptr, nullptr));
  g_hash_table_insert(attributes.get(), gpointer("key"), keyBA.data());

  bool result =
      secret_service_store_sync(service.get(), nullptr, attributes.get(), nullptr,
                                keyBA.data(), secretValue.get(), nullptr, &e);

  if (e) {
    std::string message = e->message;
    g_error_free(e);

    log::error("failed to add secret {}, {}", key, message);
    throw std::runtime_error(message);
  }

  log::debug("set secret {}", key);

  return result;
}

QString getSecret(const QString& key) noexcept(false)
{
  GError* e        = nullptr;
  QByteArray keyBA = key.toLocal8Bit();
  GHashTablePtr attributes(g_hash_table_new(nullptr, nullptr));
  g_hash_table_insert(attributes.get(), gpointer("key"), keyBA.data());

  SecretValuePtr value(
      secret_service_lookup_sync(nullptr, nullptr, attributes.get(), nullptr, &e));

  if (e) {
    log::error("failed to retrieve secret {}: {}", key, e->message);
    g_error_free(e);
    return "";
  }

  // secret not found
  if (!value) {
    log::debug("secret {} was not found", key);
    return "";
  }
  const gchar* text = secret_value_get_text(value.get());

  return QString::fromUtf8(text);
}

bool setSecret(const QString& key, const QString& data) noexcept(false)
{
  try {
    if (data.isEmpty()) {
      return deleteSecret(key);
    }
    return addSecret(key, data);
  } catch (...) {
    throw;
  }
}
