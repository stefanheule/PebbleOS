/* SPDX-FileCopyrightText: 2026 Stefan Heule */
/* SPDX-License-Identifier: Apache-2.0 */

#include "phone_prefs.h"

#include <string.h>

#include <pbl/kernel/malloc.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/pp/protocol.h>
#include <pbl/pp/send.h>
#include <pbl/services/app_session.h>
#include <pbl/services/blob_db/settings_blob_db.h>
#include <pbl/util/uuid.h>

#include <applib/app_message/app_message_internal.h>
#include <applib/dict.h>
#include <process_management/app_manager.h>
#include <shell/normal/quick_launch.h>

//! The reserved tuples of one message, applied on KernelBG.
typedef struct {
  uint16_t dict_size;
  uint8_t dict[];
} PhonePrefsJob;

static bool prv_is_quick_launch(const Tuple *t) {
  return t->key >= APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST &&
         t->key <= APP_MESSAGE_KEY_QUICK_LAUNCH_LAST && t->type == TUPLE_BYTE_ARRAY &&
         t->length == sizeof(Uuid);
}

static bool prv_is_setting(const Tuple *t) {
  return t->key >= APP_MESSAGE_KEY_SETTING_FIRST && t->key <= APP_MESSAGE_KEY_SETTING_LAST &&
         t->type == TUPLE_BYTE_ARRAY;
}

static bool prv_is_reserved(const Tuple *t) {
  return prv_is_quick_launch(t) || prv_is_setting(t) ||
         t->key == APP_MESSAGE_KEY_SETTINGS_REQUEST;
}

static bool prv_is_graphite_running(void) {
  const PebbleProcessMd *md = app_manager_get_current_app_md();
  const Uuid graphite = APP_MESSAGE_PHONE_PREFS_APP_UUID;
  return md && uuid_equal(&md->uuid, &graphite);
}

bool phone_prefs_apply_record(const uint8_t *record, size_t length) {
  if (length < 1) {
    return false;
  }
  const size_t name_len = record[0];
  if (name_len == 0 || 1 + name_len > length) {
    return false;
  }
  return settings_blob_db_set_local(&record[1], name_len, &record[1 + name_len],
                                    length - 1 - name_len);
}

static void prv_apply(void *data) {
  PhonePrefsJob *job = data;
  bool changed = false;
  bool requested = false;
  DictionaryIterator iter;
  for (Tuple *t = dict_read_begin_from_buffer(&iter, job->dict, job->dict_size); t;
       t = dict_read_next(&iter)) {
    if (prv_is_quick_launch(t)) {
      Uuid uuid;
      memcpy(&uuid, t->value->data, sizeof(uuid));
      const QuickLaunchSlot slot = t->key - APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST;
      changed |= quick_launch_set_uuid(slot, &uuid);
    } else if (prv_is_setting(t)) {
      changed |= phone_prefs_apply_record(t->value->data, t->length);
    } else if (t->key == APP_MESSAGE_KEY_SETTINGS_REQUEST) {
      requested = true;
    }
  }
  pbl_free(job);

  if (changed) {
    PBL_LOG_INFO("Watch settings changed by the phone");
  }
  if (changed || requested) {
    phone_prefs_send_dump();
  }
}

void phone_prefs_handle_app_message(const uint8_t *dict, uint16_t dict_size) {
  DictionaryIterator iter;
  size_t size = sizeof(Dictionary);
  unsigned int count = 0;
  for (Tuple *t = dict_read_begin_from_buffer(&iter, dict, dict_size); t;
       t = dict_read_next(&iter)) {
    if (prv_is_reserved(t)) {
      size += sizeof(Tuple) + t->length;
      count++;
    }
  }
  if (count == 0 || count > UINT8_MAX) {
    return;
  }

  PhonePrefsJob *job = pbl_malloc(sizeof(PhonePrefsJob) + size);
  if (!job) {
    PBL_LOG_WRN("No memory for the phone's watch settings");
    return;
  }
  job->dict_size = size;
  Dictionary *copy = (Dictionary *)job->dict;
  copy->count = count;
  uint8_t *cursor = (uint8_t *)copy->head;
  for (Tuple *t = dict_read_begin_from_buffer(&iter, dict, dict_size); t;
       t = dict_read_next(&iter)) {
    if (prv_is_reserved(t)) {
      memcpy(cursor, t, sizeof(Tuple) + t->length);
      cursor += sizeof(Tuple) + t->length;
    }
  }

  if (!pbl_sys_work_call(prv_apply, job)) {
    pbl_free(job);
  }
}

// -------------------------------------------------------------------------------------------------
// Dump

//! Largest message sent; the phone accepts at least this much.
#define DUMP_MESSAGE_MAX    PBL_PP_MAX_OUTBOUND_PAYLOAD
#define DUMP_DICT_MAX       (DUMP_MESSAGE_MAX - APP_MSG_HDR_OVRHD_SIZE)
#define DUMP_INT_TUPLE_SIZE (sizeof(Tuple) + sizeof(int32_t))
#define DUMP_RECORD_MAX     (1 + UINT8_MAX + SETTINGS_BLOB_DB_VALUE_MAX)

static int32_t s_dump_id;
static uint8_t s_transaction_id;

typedef struct {
  struct pbl_pp_session *session;
  AppMessagePush *push;
  DictionaryIterator iter;
  size_t dict_size;
  bool failed;
} DumpMessage;

static void prv_begin_message(DumpMessage *msg, int32_t dump_id) {
  const uint8_t transaction_id = APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST +
                                 (s_transaction_id++ % APP_MESSAGE_KERNEL_TRANSACTION_IDS);
  msg->push->header = (AppMessageHeader){
    .command = CMD_PUSH,
    .transaction_id = transaction_id,
  };
  msg->push->uuid = APP_MESSAGE_PHONE_PREFS_APP_UUID;
  dict_write_begin(&msg->iter, (uint8_t *)&msg->push->dictionary, DUMP_DICT_MAX);
  dict_write_int32(&msg->iter, APP_MESSAGE_KEY_SETTINGS_DUMP_ID, dump_id);
  msg->dict_size = sizeof(Dictionary) + DUMP_INT_TUPLE_SIZE;
}

static void prv_send_message(DumpMessage *msg) {
  const uint32_t dict_size = dict_write_end(&msg->iter);
  const size_t length = APP_MSG_HDR_OVRHD_SIZE + dict_size;
  if (pbl_pp_send(msg->session, APP_MESSAGE_ENDPOINT_ID, (const uint8_t *)msg->push, length,
                  PBL_PP_DEFAULT_TIMEOUT_MS) != 0) {
    PBL_LOG_WRN("Could not send the watch settings to the phone");
    msg->failed = true;
  }
}

void phone_prefs_send_dump(void) {
  if (!prv_is_graphite_running()) {
    return;
  }
  struct pbl_pp_session *session = app_session_get_current();
  if (!session) {
    return;
  }

  uint8_t *buffer = pbl_malloc(DUMP_MESSAGE_MAX + DUMP_RECORD_MAX);
  if (!buffer) {
    PBL_LOG_WRN("No memory for the watch settings dump");
    return;
  }
  uint8_t *record = buffer + DUMP_MESSAGE_MAX;
  DumpMessage msg = {
    .session = session,
    .push = (AppMessagePush *)buffer,
  };
  const int32_t dump_id = ++s_dump_id;
  prv_begin_message(&msg, dump_id);

  int32_t count = 0;
  for (size_t i = 0; i < settings_blob_db_num_synced() && !msg.failed; i++) {
    const char *name = settings_blob_db_synced_name(i);
    const size_t name_len = strlen(name);
    if (name_len > UINT8_MAX) {
      continue;
    }
    uint8_t *value = &record[1 + name_len];
    const int size = settings_blob_db_get_current((const uint8_t *)name, name_len, value,
                                                  SETTINGS_BLOB_DB_VALUE_MAX);
    if (size <= 0 || size > SETTINGS_BLOB_DB_VALUE_MAX) {
      continue;
    }
    if (APP_MESSAGE_KEY_SETTINGS_DUMP_FIRST + count >= APP_MESSAGE_KEY_SETTINGS_DUMP_ID) {
      break;
    }
    record[0] = name_len;
    memcpy(&record[1], name, name_len);
    const size_t record_len = 1 + name_len + size;

    // Always leave room for the count that ends the last message:
    const size_t tuple_size = sizeof(Tuple) + record_len;
    if (msg.dict_size + tuple_size + DUMP_INT_TUPLE_SIZE > DUMP_DICT_MAX) {
      prv_send_message(&msg);
      if (msg.failed) {
        break;
      }
      prv_begin_message(&msg, dump_id);
    }
    dict_write_data(&msg.iter, APP_MESSAGE_KEY_SETTINGS_DUMP_FIRST + count, record, record_len);
    msg.dict_size += tuple_size;
    count++;
  }

  if (!msg.failed) {
    dict_write_int32(&msg.iter, APP_MESSAGE_KEY_SETTINGS_DUMP_COUNT, count);
    prv_send_message(&msg);
  }
  pbl_free(buffer);
}
