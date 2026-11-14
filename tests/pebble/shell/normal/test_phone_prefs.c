/* SPDX-FileCopyrightText: 2026 Stefan Heule */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/kernel/work.h>
#include <pbl/pp/protocol.h>
#include <pbl/services/blob_db/app_db.h>
#include <pbl/services/blob_db/settings_blob_db.h>
#include <pbl/util/size.h>
#include <pbl/util/uuid.h>

#include <applib/app_message/app_message_internal.h>
#include <applib/dict.h>
#include <clar.h>
#include <process_management/app_install_manager.h>
#include <process_management/pebble_process_md.h>
#include <shell/normal/phone_prefs.h>
#include <shell/normal/quick_launch.h>

#include <fake_pbl_malloc.h>

// Fakes
////////////////////////////////////////////////////////////////

//! The synced settings and their sizes on obelix, except two the watch doesn't keep (size 0).
static const struct {
  const char *name;
  int size;
} s_synced[] = {
  {"clock24h", 1},
  {"timezoneSource", 1},
  {"automaticTimezoneID", 2},
  {"unitsDistance", 1},
  {"unitsWind", 1},
  {"unitsTemperature", 1},
  {"textStyle", 1},
  {"systemTextSize", 1},
  {"stationaryMode", 1},
  {"displayOrientationLeftHanded", 1},
  {"motionSensitivity", 1},
  {"lightEnabled", 1},
  {"lightAmbientSensorEnabled", 1},
  {"lightTimeoutMs", 4},
  {"lightIntensity", 1},
  {"lightMotion", 1},
  {"lightTouch", 1},
  {"lightAmbientThreshold", 4},
  {"lightDynamicMode", 1},
  {"lightPreset", 1},
  {"lightColor", 4},
  {"lightColorDayNightEnabled", 1},
  {"lightColorNight", 4},
  {"lightColorSunriseMinute", 2},
  {"lightColorSunsetMinute", 2},
  {"langEnglish", 1},
  {"language", 1},
  {"qlUp", 17},
  {"qlDown", 17},
  {"qlSelect", 17},
  {"qlBack", 17},
  {"qlSetupOpened", 1},
  {"qlSingleClickUp", 17},
  {"qlSingleClickDown", 17},
  {"qlComboBackUp", 17},
  {"qlComboUpDown", 17},
  {"settingsMenuHighlightColor", 0},
  {"appsMenuHighlightColor", 0},
  {"timelineQuickViewEnabled", 1},
  {"timelineQuickViewBeforeTimeMin", 2},
  {"timelineQuickViewWatchfaceFit", 1},
  {"timelineSettingsOpened", 1},
  {"activityPreferences", 9},
  {"activityHealthAppOpened", 1},
  {"activityWorkoutAppOpened", 1},
  {"alarmsAppOpened", 1},
  {"hrmPreferences", 3},
  {"heartRatePreferences", 6},
  {"spo2Preferences", 3},
  {"menuScrollWrapAround", 1},
  {"menuScrollVibeBehavior", 1},
  {"workerId", 16},
  {"musicShowVolumeControls", 1},
  {"musicShowProgressBar", 1},
  {"musicShowAlbumArt", 1},
  {"mask", 1},
  {"dndInterruptionsMask", 1},
  {"dndShowNotifications", 1},
  {"vibeIntensity", 1},
  {"vibeScoreNotifications", 1},
  {"vibeScoreIncomingCalls", 1},
  {"vibeScoreAlarms", 1},
  {"dndManuallyEnabled", 1},
  {"dndSmartEnabled", 1},
  {"dndWeekdaySchedule", 4},
  {"dndWeekdayScheduleEnabled", 1},
  {"dndWeekendSchedule", 4},
  {"dndWeekendScheduleEnabled", 1},
  {"qtSchedule0", 9},
  {"qtSchedule1", 9},
  {"qtSchedule2", 9},
  {"qtSchedule3", 9},
  {"qtSchedule4", 9},
  {"notifWindowTimeout", 4},
  {"notifTextSize", 1},
  {"notifDesignStyle", 1},
  {"notifVibeDelay", 1},
  {"notifBacklight", 1},
  {"notifRespectPhoneSilence", 1},
  {"notifDismissalSync", 1},
  {"dndMotionBacklight", 1},
  {"dndTouchBacklight", 1},
  {"dndNotificationBacklight", 1},
  {"dndAutoDismiss", 1},
};

//! A setting's value: its index in every byte, so a record shows whose value it carries.
static int prv_index_of(const uint8_t *name, size_t name_len) {
  for (size_t i = 0; i < ARRAY_LENGTH(s_synced); i++) {
    if (strlen(s_synced[i].name) == name_len && memcmp(s_synced[i].name, name, name_len) == 0) {
      return i;
    }
  }
  return -1;
}

size_t settings_blob_db_num_synced(void) {
  return ARRAY_LENGTH(s_synced);
}

const char *settings_blob_db_synced_name(size_t index) {
  return (index < ARRAY_LENGTH(s_synced)) ? s_synced[index].name : nullptr;
}

int settings_blob_db_get_current(const uint8_t *name, size_t name_len, void *val_out,
                                 size_t val_max) {
  const int i = prv_index_of(name, name_len);
  if (i < 0) {
    return 0;
  }
  memset(val_out, i, MIN((size_t)s_synced[i].size, val_max));
  return s_synced[i].size;
}

static int s_set_local_calls;
static char s_set_local_name[32];
static size_t s_set_local_len;
static bool s_set_local_result;
bool settings_blob_db_set_local(const uint8_t *name, size_t name_len, const uint8_t *val,
                                size_t val_len) {
  s_set_local_calls++;
  cl_assert(name_len < sizeof(s_set_local_name));
  memcpy(s_set_local_name, name, name_len);
  s_set_local_name[name_len] = '\0';
  s_set_local_len = val_len;
  return s_set_local_result;
}

static int s_quick_launch_calls;
static QuickLaunchSlot s_quick_launch_slot;
static bool s_quick_launch_result;
bool quick_launch_set_uuid(QuickLaunchSlot slot, const Uuid *uuid) {
  s_quick_launch_calls++;
  s_quick_launch_slot = slot;
  return s_quick_launch_result;
}

static PebbleProcessMd s_app_md;
const PebbleProcessMd *app_manager_get_current_app_md(void) {
  return &s_app_md;
}

static struct pbl_pp_session *s_session = (struct pbl_pp_session *)0x1234;
struct pbl_pp_session *app_session_get_current(void) {
  return s_session;
}

#define MAX_SENT 16
static int s_sent_count;
static size_t s_sent_length[MAX_SENT];
static uint8_t s_sent[MAX_SENT][2048];
static int s_send_result;
int pbl_pp_send(struct pbl_pp_session *session, uint16_t endpoint_id, const uint8_t *data,
                size_t length, uint32_t timeout_ms) {
  cl_assert_equal_p(session, s_session);
  cl_assert_equal_i(endpoint_id, APP_MESSAGE_ENDPOINT_ID);
  cl_assert(s_sent_count < MAX_SENT);
  cl_assert(length <= sizeof(s_sent[0]));
  memcpy(s_sent[s_sent_count], data, length);
  s_sent_length[s_sent_count] = length;
  s_sent_count++;
  return s_send_result;
}

#define MAX_APPS 40
static AppDBEntry s_app_db[MAX_APPS];
static int s_app_db_count;
void app_db_enumerate_entries(AppDBEnumerateCb cb, void *data) {
  for (int i = 0; i < s_app_db_count; i++) {
    cb(i + 1, &s_app_db[i], data);
  }
}

//! A built-in app that the phone also has an app_db entry for.
static const Uuid s_builtin_uuid = {0x0b, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
AppInstallId app_get_install_id_for_uuid_from_registry(const Uuid *uuid) {
  return uuid_equal(uuid, &s_builtin_uuid) ? -5 : INSTALL_ID_INVALID;
}

static AppInstallCallbackNode *s_install_callbacks;
void app_install_register_callback(struct AppInstallCallbackNode *callback_info) {
  s_install_callbacks = callback_info;
}

static struct pbl_delayed_work *s_scheduled;
bool pbl_delayed_work_reschedule(struct pbl_workq *q, struct pbl_delayed_work *dwork,
                                 pbl_timeout_t delay) {
  cl_assert_equal_p(q, &pbl_sys_workq);
  s_scheduled = dwork;
  return false;
}

//! Runs the scheduled check, if any.
static bool prv_run_scheduled(void) {
  struct pbl_delayed_work *work = s_scheduled;
  s_scheduled = nullptr;
  if (work) {
    work->work.fn(&work->work);
  }
  return work != nullptr;
}

static void prv_add_app(const Uuid *uuid, uint8_t major, uint8_t minor, uint32_t info_flags) {
  cl_assert(s_app_db_count < MAX_APPS);
  s_app_db[s_app_db_count++] = (AppDBEntry){
    .uuid = *uuid,
    .info_flags = info_flags,
    .app_version = {major, minor},
  };
}

static Uuid prv_app_uuid(int i) {
  return (Uuid){0xa0, (uint8_t)i, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
}

//! Installs @p n third-party apps, every third a watchface, plus entries the dump must skip.
static void prv_install_apps(int n) {
  s_app_db_count = 0;
  const Uuid system = UUID_SYSTEM;
  prv_add_app(&system, 1, 0, 0);
  prv_add_app(&s_builtin_uuid, 1, 0, 0);
  for (int i = 0; i < n; i++) {
    const Uuid uuid = prv_app_uuid(i);
    prv_add_app(&uuid, i, 2 * i, (i % 3 == 0) ? PROCESS_INFO_WATCH_FACE : PROCESS_INFO_HAS_WORKER);
  }
}

static int prv_num_apps(void) {
  return s_app_db_count >= 2 ? s_app_db_count - 2 : 0;
}

static int prv_num_records(void) {
  return ARRAY_LENGTH(s_synced) - 2 + prv_num_apps();
}

// Helpers
////////////////////////////////////////////////////////////////

void test_phone_prefs__initialize(void) {
  s_app_md = (PebbleProcessMd){.uuid = APP_MESSAGE_PHONE_PREFS_APP_UUID};
  s_session = (struct pbl_pp_session *)0x1234;
  s_sent_count = 0;
  s_send_result = 0;
  s_set_local_calls = 0;
  s_set_local_result = false;
  s_quick_launch_calls = 0;
  s_quick_launch_result = false;
  s_app_db_count = 0;
  s_scheduled = nullptr;
}

void test_phone_prefs__cleanup(void) {
}

static size_t prv_record(uint8_t *out, const char *name, size_t value_len) {
  const size_t name_len = strlen(name);
  out[0] = name_len;
  memcpy(&out[1], name, name_len);
  memset(&out[1 + name_len], 0x5a, value_len);
  return 1 + name_len + value_len;
}

//! Checks the messages sent as one dump, and returns its record count.
static int prv_check_dump(int first, int last, int32_t *dump_id_out) {
  const Uuid graphite = APP_MESSAGE_PHONE_PREFS_APP_UUID;
  int records = 0;
  int apps = 0;
  int32_t dump_id = 0;
  int setting = 0;
  for (int m = first; m <= last; m++) {
    cl_assert(s_sent_length[m] <= PBL_PP_MAX_OUTBOUND_PAYLOAD);
    const AppMessagePush *push = (const AppMessagePush *)s_sent[m];
    cl_assert_equal_i(push->header.command, CMD_PUSH);
    cl_assert(push->header.transaction_id >= APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST);
    cl_assert(uuid_equal(&push->uuid, &graphite));

    bool has_id = false;
    bool has_count = false;
    DictionaryIterator iter;
    for (Tuple *t = dict_read_begin_from_buffer(&iter, (const uint8_t *)&push->dictionary,
                                                s_sent_length[m] - APP_MSG_HDR_OVRHD_SIZE);
         t; t = dict_read_next(&iter)) {
      if (t->key == APP_MESSAGE_KEY_SETTINGS_DUMP_ID) {
        cl_assert_equal_i(t->type, TUPLE_INT);
        if (m == first) {
          dump_id = t->value->int32;
        }
        cl_assert_equal_i(t->value->int32, dump_id);
        has_id = true;
      } else if (t->key == APP_MESSAGE_KEY_SETTINGS_DUMP_COUNT) {
        cl_assert_equal_i(t->type, TUPLE_INT);
        cl_assert_equal_i(t->value->int32, records);
        has_count = true;
      } else if (t->length > 4 && memcmp(&t->value->data[1], "app/", 4) == 0) {
        // Then the third-party apps, in app_db order, after every setting:
        cl_assert_equal_i(t->key, APP_MESSAGE_KEY_SETTINGS_DUMP_FIRST + records);
        cl_assert_equal_i(t->type, TUPLE_BYTE_ARRAY);
        cl_assert_equal_i(records - apps, ARRAY_LENGTH(s_synced) - 2);
        const AppDBEntry *entry = &s_app_db[2 + apps];
        const PhonePrefsApp app = {
          .uuid = entry->uuid,
          .version = entry->app_version,
          .flags = (entry->info_flags & PROCESS_INFO_WATCH_FACE) ? 1 : 0,
        };
        uint8_t expected[64];
        const size_t expected_len = phone_prefs_app_record(expected, &app);
        cl_assert_equal_i(t->length, expected_len);
        cl_assert(memcmp(t->value->data, expected, expected_len) == 0);
        records++;
        apps++;
      } else {
        // Records are numbered without gaps, in whitelist order, skipping unkept settings:
        cl_assert_equal_i(apps, 0);
        cl_assert_equal_i(t->key, APP_MESSAGE_KEY_SETTINGS_DUMP_FIRST + records);
        cl_assert_equal_i(t->type, TUPLE_BYTE_ARRAY);
        while (s_synced[setting].size == 0) {
          setting++;
        }
        const char *name = s_synced[setting].name;
        const size_t name_len = strlen(name);
        cl_assert_equal_i(t->length, 1 + name_len + s_synced[setting].size);
        cl_assert_equal_i(t->value->data[0], name_len);
        cl_assert(memcmp(&t->value->data[1], name, name_len) == 0);
        for (int b = 0; b < s_synced[setting].size; b++) {
          cl_assert_equal_i(t->value->data[1 + name_len + b], setting);
        }
        records++;
        setting++;
      }
    }
    cl_assert(has_id);
    cl_assert_equal_b(has_count, m == last);
  }
  cl_assert_equal_i(apps, prv_num_apps());
  if (dump_id_out) {
    *dump_id_out = dump_id;
  }
  return records;
}

// Tests
////////////////////////////////////////////////////////////////

void test_phone_prefs__apply_record(void) {
  uint8_t record[64];
  s_set_local_result = true;
  cl_assert_equal_b(phone_prefs_apply_record(record, prv_record(record, "clock24h", 1)), true);
  cl_assert_equal_i(s_set_local_calls, 1);
  cl_assert_equal_s(s_set_local_name, "clock24h");
  cl_assert_equal_i(s_set_local_len, 1);

  // The name may take the whole record; the setting then rejects the empty value:
  cl_assert_equal_b(phone_prefs_apply_record(record, prv_record(record, "clock24h", 0)), true);
  cl_assert_equal_i(s_set_local_len, 0);
  cl_assert_equal_i(s_set_local_calls, 2);

  // Malformed records never reach the settings:
  cl_assert_equal_b(phone_prefs_apply_record(record, 0), false);
  record[0] = 0;
  cl_assert_equal_b(phone_prefs_apply_record(record, 3), false);
  const size_t length = prv_record(record, "clock24h", 0);
  cl_assert_equal_b(phone_prefs_apply_record(record, length - 1), false);
  cl_assert_equal_i(s_set_local_calls, 2);
}

void test_phone_prefs__dump(void) {
  phone_prefs_send_dump();
  cl_assert(s_sent_count > 1);
  int32_t dump_id;
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, &dump_id), prv_num_records());
  printf("Dump of %d records: %d messages of", (int)ARRAY_LENGTH(s_synced) - 2, s_sent_count);
  for (int m = 0; m < s_sent_count; m++) {
    printf(" %d", (int)s_sent_length[m]);
  }
  printf(" bytes\n");

  // Every dump has a new id:
  const int first = s_sent_count;
  phone_prefs_send_dump();
  int32_t next_id;
  prv_check_dump(first, s_sent_count - 1, &next_id);
  cl_assert_equal_i(next_id, dump_id + 1);
}

void test_phone_prefs__dump_needs_graphite_and_session(void) {
  s_session = nullptr;
  phone_prefs_send_dump();
  s_session = (struct pbl_pp_session *)0x1234;
  s_app_md.uuid = UuidMake(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
  phone_prefs_send_dump();
  cl_assert_equal_i(s_sent_count, 0);
}

void test_phone_prefs__dump_stops_on_send_failure(void) {
  s_send_result = -1;
  phone_prefs_send_dump();
  cl_assert_equal_i(s_sent_count, 1);
}

//! Hands the kernel a message with the given tuples, plus an app key.
static void prv_handle(const Tuplet *tuplets, uint8_t count) {
  uint8_t buffer[512];
  DictionaryIterator iter;
  dict_write_begin(&iter, buffer, sizeof(buffer));
  dict_write_uint32(&iter, 1, 42);
  for (uint8_t i = 0; i < count; i++) {
    cl_assert_equal_i(dict_write_tuplet(&iter, &tuplets[i]), DICT_OK);
  }
  const uint32_t size = dict_write_end(&iter);
  phone_prefs_handle_app_message(buffer, size);
}

void test_phone_prefs__handle_message(void) {
  const Uuid uuid = UuidMake(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
  uint8_t record_a[32];
  uint8_t record_b[32];
  const Tuplet tuplets[] = {
    TupletBytes(APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST + 5, (const uint8_t *)&uuid, sizeof(uuid)),
    // Too short for a UUID:
    TupletBytes(APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST, (const uint8_t *)&uuid, 4),
    TupletBytes(APP_MESSAGE_KEY_SETTING_FIRST, record_a, prv_record(record_a, "clock24h", 1)),
    TupletBytes(APP_MESSAGE_KEY_SETTING_LAST, record_b, prv_record(record_b, "lightTimeoutMs", 4)),
    // Not byte arrays, or past the range:
    TupletInteger(APP_MESSAGE_KEY_SETTING_FIRST + 1, 1),
    TupletBytes(APP_MESSAGE_KEY_SETTING_LAST + 1, record_a, prv_record(record_a, "clock24h", 1)),
  };

  // Nothing changed and no request: no dump.
  prv_handle(tuplets, ARRAY_LENGTH(tuplets));
  cl_assert_equal_i(s_quick_launch_calls, 1);
  cl_assert_equal_i(s_quick_launch_slot, QuickLaunchSlotBack);
  cl_assert_equal_i(s_set_local_calls, 2);
  cl_assert_equal_s(s_set_local_name, "lightTimeoutMs");
  cl_assert_equal_i(s_sent_count, 0);

  // A change sends one dump:
  s_set_local_result = true;
  prv_handle(tuplets, ARRAY_LENGTH(tuplets));
  cl_assert(s_sent_count > 1);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());
  s_set_local_result = false;

  // So does a quick launch change:
  s_sent_count = 0;
  s_quick_launch_result = true;
  prv_handle(tuplets, 1);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());
  s_quick_launch_result = false;

  // And a request, of any type:
  const Tuplet request = TupletInteger(APP_MESSAGE_KEY_SETTINGS_REQUEST, (uint8_t)1);
  s_sent_count = 0;
  prv_handle(&request, 1);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());

  // A message without reserved keys is left alone:
  s_sent_count = 0;
  s_set_local_calls = 0;
  prv_handle(nullptr, 0);
  cl_assert_equal_i(s_set_local_calls, 0);
  cl_assert_equal_i(s_sent_count, 0);
}

void test_phone_prefs__app_record(void) {
  const PhonePrefsApp app = {
    .uuid = UuidMake(0x7e, 0x52, 0x67, 0xac, 0x79, 0x8f, 0x49, 0x53, 0xa6, 0x45, 0x1b, 0x87, 0xa5,
                     0xc2, 0x9d, 0x96),
    .version = {2, 20},
    .flags = PHONE_PREFS_APP_FLAG_WATCHFACE,
  };
  uint8_t record[64];
  const size_t length = phone_prefs_app_record(record, &app);
  const char name[] = "app/7e5267ac-798f-4953-a645-1b87a5c29d96";
  cl_assert_equal_i(length, 1 + 40 + 3);
  cl_assert_equal_i(record[0], 40);
  cl_assert(memcmp(&record[1], name, 40) == 0);
  cl_assert_equal_i(record[41], 2);
  cl_assert_equal_i(record[42], 20);
  cl_assert_equal_i(record[43], 1);
}

void test_phone_prefs__dump_with_apps(void) {
  // Built-in apps are left out; 25 apps still fit 656-byte messages:
  prv_install_apps(25);
  phone_prefs_send_dump();
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());
  printf("Dump of %d records with %d apps: %d messages of", prv_num_records(), prv_num_apps(),
         s_sent_count);
  for (int m = 0; m < s_sent_count; m++) {
    printf(" %d", (int)s_sent_length[m]);
  }
  printf(" bytes\n");

  // And with many more:
  prv_install_apps(MAX_APPS - 2);
  s_sent_count = 0;
  phone_prefs_send_dump();
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());
}

static void prv_install_event(InstallEventType type) {
  cl_assert(s_install_callbacks);
  s_install_callbacks->callbacks[type](1, s_install_callbacks->data);
}

void test_phone_prefs__app_change_sends_pending_dump(void) {
  static bool s_initialized;
  if (!s_initialized) {
    phone_prefs_init();
    s_initialized = true;
  }
  const Uuid graphite = APP_MESSAGE_PHONE_PREFS_APP_UUID;
  const Uuid other = UuidMake(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
  prv_install_apps(3);
  phone_prefs_send_dump();
  s_sent_count = 0;

  // An event that changes nothing sends nothing:
  prv_install_event(APP_ICON_NAME_UPDATED);
  cl_assert_equal_b(prv_run_scheduled(), true);
  cl_assert_equal_i(s_sent_count, 0);
  phone_prefs_handle_app_running(&graphite);
  cl_assert_equal_p(s_scheduled, nullptr);

  // A new version, installed while another app is in front, waits for Graphite:
  s_app_md.uuid = other;
  s_app_db[3].app_version.minor++;
  prv_install_event(APP_AVAILABLE);
  cl_assert_equal_b(prv_run_scheduled(), true);
  cl_assert_equal_i(s_sent_count, 0);
  phone_prefs_handle_app_running(&other);
  cl_assert_equal_p(s_scheduled, nullptr);

  // ...then goes out once Graphite runs with a phone connection:
  s_app_md.uuid = graphite;
  s_session = nullptr;
  phone_prefs_handle_app_running(&graphite);
  cl_assert_equal_b(prv_run_scheduled(), true);
  cl_assert_equal_i(s_sent_count, 0);
  s_session = (struct pbl_pp_session *)0x1234;
  phone_prefs_handle_app_running(&graphite);
  cl_assert_equal_b(prv_run_scheduled(), true);
  cl_assert(s_sent_count > 0);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());

  // and only once:
  s_sent_count = 0;
  phone_prefs_handle_app_running(&graphite);
  cl_assert_equal_b(prv_run_scheduled(), false);

  // A removed app is a change too, and is sent at once while Graphite runs:
  s_app_db_count--;
  prv_install_event(APP_REMOVED);
  cl_assert_equal_b(prv_run_scheduled(), true);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), prv_num_records());
}
