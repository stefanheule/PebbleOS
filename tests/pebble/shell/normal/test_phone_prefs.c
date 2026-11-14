/* SPDX-FileCopyrightText: 2026 Stefan Heule */
/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <pbl/pp/protocol.h>
#include <pbl/services/blob_db/settings_blob_db.h>
#include <pbl/util/size.h>
#include <pbl/util/uuid.h>

#include <applib/app_message/app_message_internal.h>
#include <applib/dict.h>
#include <clar.h>
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
      } else {
        // Records are numbered without gaps, in whitelist order, skipping unkept settings:
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
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, &dump_id), ARRAY_LENGTH(s_synced) - 2);
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
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), ARRAY_LENGTH(s_synced) - 2);
  s_set_local_result = false;

  // So does a quick launch change:
  s_sent_count = 0;
  s_quick_launch_result = true;
  prv_handle(tuplets, 1);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), ARRAY_LENGTH(s_synced) - 2);
  s_quick_launch_result = false;

  // And a request, of any type:
  const Tuplet request = TupletInteger(APP_MESSAGE_KEY_SETTINGS_REQUEST, (uint8_t)1);
  s_sent_count = 0;
  prv_handle(&request, 1);
  cl_assert_equal_i(prv_check_dump(0, s_sent_count - 1, nullptr), ARRAY_LENGTH(s_synced) - 2);

  // A message without reserved keys is left alone:
  s_sent_count = 0;
  s_set_local_calls = 0;
  prv_handle(nullptr, 0);
  cl_assert_equal_i(s_set_local_calls, 0);
  cl_assert_equal_i(s_sent_count, 0);
}
