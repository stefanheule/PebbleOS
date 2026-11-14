/* SPDX-FileCopyrightText: 2026 Stefan Heule */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/fs/fs.h>
#include <pbl/services/blob_db/settings_blob_db.h>
#include <pbl/services/notifications/alerts_preferences_private.h>
#include <pbl/util/list.h>
#include <pbl/util/size.h>

#include <clar.h>
#include <shell/prefs.h>
#include <shell/prefs_private.h>

// Fakes
////////////////////////////////////////////////////////////////
#include <fake_kernel_services_notifications.h>
#include <fake_pbl_malloc.h>
#include <fake_pfs.h>
#include <fake_spi_flash.h>

// Stubs
////////////////////////////////////////////////////////////////
#include <stubs_dark_mode_updater.h>

void prefs_sync_init(void) {
}

void event_put(PebbleEvent *event) {
}

void language_pack_enable(bool enable) {
}

//! One notification pref, as alerts_preferences keeps them.
static bool s_dnd_auto_dismiss;
static int s_notif_reloads;

int alerts_preferences_get_current(const char *key, void *value_out, size_t max_len) {
  if (strcmp(key, "dndAutoDismiss") != 0) {
    return 0;
  }
  memcpy(value_out, &s_dnd_auto_dismiss, MIN(max_len, sizeof(s_dnd_auto_dismiss)));
  return sizeof(s_dnd_auto_dismiss);
}

void alerts_preferences_handle_blob_db_event(PebbleBlobDBEvent *event) {
  cl_assert_equal_i(event->key_len, strlen("dndAutoDismiss"));
  s_dnd_auto_dismiss = true;
  s_notif_reloads++;
}

void alerts_preferences_lock(void) {
}

void alerts_preferences_unlock(void) {
}

// Helpers
////////////////////////////////////////////////////////////////

static bool prv_set(const char *name, const void *value, size_t len) {
  return settings_blob_db_set_local((const uint8_t *)name, strlen(name), value, len);
}

//! Whether @p name is dirty, i.e. queued to sync to the phone.
static bool prv_is_dirty(const char *name) {
  bool found = false;
  BlobDBDirtyItem *item = settings_blob_db_get_dirty_list();
  while (item) {
    BlobDBDirtyItem *next = (BlobDBDirtyItem *)item->node.next;
    if ((size_t)item->key_len >= strlen(name) && memcmp(item->key, name, strlen(name)) == 0 &&
        ((size_t)item->key_len == strlen(name) || item->key[strlen(name)] == '\0')) {
      found = true;
    }
    pbl_free(item);
    item = next;
  }
  return found;
}

void test_settings_blob_db_local__initialize(void) {
  fake_spi_flash_init(0, 0x1000000);
  fake_pfs_mount();
  settings_blob_db_init();
  s_dnd_auto_dismiss = false;
  s_notif_reloads = 0;
}

void test_settings_blob_db_local__cleanup(void) {
}

// Tests
////////////////////////////////////////////////////////////////

void test_settings_blob_db_local__names_and_sizes(void) {
  const size_t num = settings_blob_db_num_synced();
  cl_assert(num > 0);
  cl_assert_equal_s(settings_blob_db_synced_name(0), "clock24h");
  cl_assert_equal_p(settings_blob_db_synced_name(num), nullptr);

  bool has_notif = false;
  for (size_t i = 0; i < num; i++) {
    const char *name = settings_blob_db_synced_name(i);
    uint8_t value[SETTINGS_BLOB_DB_VALUE_MAX];
    const int size =
        settings_blob_db_get_current((const uint8_t *)name, strlen(name), value, sizeof(value));
    cl_assert(size <= SETTINGS_BLOB_DB_VALUE_MAX);
    has_notif |= (strcmp(name, "dndAutoDismiss") == 0);
  }
  cl_assert(has_notif);

  uint8_t value[SETTINGS_BLOB_DB_VALUE_MAX];
  cl_assert_equal_i(settings_blob_db_get_current((const uint8_t *)"qlUp", 4, value, sizeof(value)),
                    17);
  cl_assert_equal_i(
      settings_blob_db_get_current((const uint8_t *)"lightTimeoutMs", 14, value, sizeof(value)),
      4);
  // Exact names only, and only whitelisted ones:
  cl_assert_equal_i(settings_blob_db_get_current((const uint8_t *)"clock24h", 9, value, 64), 0);
  cl_assert_equal_i(settings_blob_db_get_current((const uint8_t *)"clock", 5, value, 64), 0);
  cl_assert_equal_i(
      settings_blob_db_get_current((const uint8_t *)"welcomeVersion", 14, value, 64), 0);
}

void test_settings_blob_db_local__set_shell_pref(void) {
  shell_prefs_set_clock_24h_style(false);
  cl_assert_equal_b(prv_is_dirty("clock24h"), true);
  settings_blob_db_mark_synced((const uint8_t *)"clock24h", sizeof("clock24h"));
  cl_assert_equal_b(prv_is_dirty("clock24h"), false);

  // The same value writes nothing:
  const uint8_t off = 0;
  cl_assert_equal_b(prv_set("clock24h", &off, 1), false);
  cl_assert_equal_b(prv_is_dirty("clock24h"), false);

  // A change applies at once and syncs to the phone like a change on the watch:
  const uint8_t on = 1;
  cl_assert_equal_b(prv_set("clock24h", &on, 1), true);
  cl_assert_equal_b(shell_prefs_get_clock_24h_style(), true);
  cl_assert_equal_b(prv_is_dirty("clock24h"), true);
  uint8_t stored = 0;
  cl_assert_equal_i(settings_blob_db_read((const uint8_t *)"clock24h", 8, &stored, 1), S_SUCCESS);
  cl_assert_equal_i(stored, 1);
  cl_assert_equal_b(prv_set("clock24h", &on, 1), false);

  const uint32_t timeout = 7000;
  cl_assert_equal_b(prv_set("lightTimeoutMs", &timeout, sizeof(timeout)), true);
  cl_assert_equal_i(backlight_get_timeout_ms(), 7000);
}

void test_settings_blob_db_local__rejects(void) {
  shell_prefs_set_clock_24h_style(false);
  const uint8_t value[4] = {1, 1, 1, 1};
  // Wrong length:
  cl_assert_equal_b(prv_set("clock24h", value, 2), false);
  cl_assert_equal_b(prv_set("clock24h", value, 0), false);
  // Not whitelisted, unknown, or whitelisted but not kept by this watch:
  cl_assert_equal_b(prv_set("welcomeVersion", value, 1), false);
  cl_assert_equal_b(prv_set("noSuchSetting", value, 1), false);
  cl_assert_equal_b(prv_set("settingsMenuHighlightColor", value, 1), false);
  cl_assert_equal_b(settings_blob_db_set_local((const uint8_t *)"clock24h", 9, value, 1), false);
  cl_assert_equal_b(shell_prefs_get_clock_24h_style(), false);
}

void test_settings_blob_db_local__set_notif_pref(void) {
  const bool on = true;
  cl_assert_equal_b(prv_set("dndAutoDismiss", &on, sizeof(on)), true);
  cl_assert_equal_i(s_notif_reloads, 1);
  cl_assert_equal_b(prv_is_dirty("dndAutoDismiss"), true);
  cl_assert_equal_b(prv_set("dndAutoDismiss", &on, sizeof(on)), false);
  cl_assert_equal_i(s_notif_reloads, 1);
}

void test_settings_blob_db_local__phone_insert_stays_synced(void) {
  const uint8_t on = 1;
  cl_assert_equal_i(settings_blob_db_insert((const uint8_t *)"lightMotion", 11, &on, 1), S_SUCCESS);
  cl_assert_equal_b(prv_is_dirty("lightMotion"), false);
}
