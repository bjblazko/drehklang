#pragma once

#include <Arduino.h>
#include <nvs.h>

#include <vector>

namespace dialhard::drivers {

// The product was renamed from "knobify" to "DialHard" (2026-10-05), and
// with it the NVS namespace NvsKeyValueStore uses. This moves a device's
// existing settings and resume record across once, at boot, before
// anything reads them.
//
// Copy first, erase the old namespace last: a power cut part-way through
// leaves the old namespace intact, so the next boot simply copies again.
// Only the two value types the store writes (u8 and blobs) are carried.
constexpr const char *kLegacyNvsNamespace = "knobify";

inline bool copyNvsEntry(nvs_handle_t from, nvs_handle_t to,
                         const nvs_entry_info_t &entry) {
  if (entry.type == NVS_TYPE_U8) {
    uint8_t value = 0;
    return nvs_get_u8(from, entry.key, &value) == ESP_OK &&
           nvs_set_u8(to, entry.key, value) == ESP_OK;
  }
  if (entry.type == NVS_TYPE_BLOB) {
    size_t length = 0;
    if (nvs_get_blob(from, entry.key, nullptr, &length) != ESP_OK) return false;
    std::vector<uint8_t> bytes(length);
    return nvs_get_blob(from, entry.key, bytes.data(), &length) == ESP_OK &&
           nvs_set_blob(to, entry.key, bytes.data(), length) == ESP_OK;
  }
  Serial.printf("[migrate] NVS key %s has an unexpected type, skipped\n",
                entry.key);
  return true;
}

// Consumes the iterator: nvs_entry_next() releases it at the end.
inline bool copyNvsNamespace(nvs_iterator_t it, nvs_handle_t from,
                             nvs_handle_t to) {
  bool ok = true;
  while (it != nullptr) {
    nvs_entry_info_t entry;
    nvs_entry_info(it, &entry);
    ok = copyNvsEntry(from, to, entry) && ok;
    it = nvs_entry_next(it);
  }
  return ok && nvs_commit(to) == ESP_OK;
}

inline void migrateLegacyNvsNamespace(const char *newNamespace) {
  // Looked up by entries rather than opened: opening read-write would
  // create the old namespace on every boot.
  nvs_iterator_t it =
      nvs_entry_find(NVS_DEFAULT_PART_NAME, kLegacyNvsNamespace, NVS_TYPE_ANY);
  if (it == nullptr) return;
  nvs_handle_t from;
  nvs_handle_t to;
  if (nvs_open(kLegacyNvsNamespace, NVS_READWRITE, &from) != ESP_OK) {
    nvs_release_iterator(it);
    return;
  }
  if (nvs_open(newNamespace, NVS_READWRITE, &to) != ESP_OK) {
    nvs_release_iterator(it);
    nvs_close(from);
    return;
  }
  if (copyNvsNamespace(it, from, to)) {
    nvs_erase_all(from);
    nvs_commit(from);
    Serial.printf("[migrate] NVS %s -> %s\n", kLegacyNvsNamespace,
                  newNamespace);
  } else {
    Serial.println("[migrate] NVS copy failed, will retry next boot");
  }
  nvs_close(to);
  nvs_close(from);
}

}  // namespace dialhard::drivers
