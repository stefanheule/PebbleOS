/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <limits.h>
#include <stddef.h>

#include <pbl/kernel/compiler.h>
#include <pbl/logging/logging.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#include <applib/app_message/app_message_internal.h>
#include <clar.h>
#include <kernel/events.h>

extern AppTimer *app_message_outbox_get_ack_nack_timer(void);

// Stubs
////////////////////////////////////
#include <fake_pbl_malloc.h>

// Fakes
////////////////////////////////////
#include <pbl/kernel/malloc.h>

#include <fake_app_timer.h>
#include <fake_pebble_tasks.h>

// Structures and Externs
////////////////////////////////////
typedef struct PBL_PACKED {
  AppMessageCmd command : 8;
  uint8_t transaction_id;
  union PBL_PACKED {
    struct PBL_PACKED {
      Uuid uuid;
      Dictionary dictionary; //!< Variable length!
    } push;                  //!< valid for CMD_PUSH only
    struct PBL_PACKED {
    } ack;
  } payload[];
} AppMessage;

extern AppTimer *app_message_ack_timer_id(void);
extern bool app_message_is_accepting_inbound(void);
extern bool app_message_is_accepting_outbound(void);
extern bool app_message_is_closed_inbound(void);
extern bool app_message_is_closed_outbound(void);
extern void app_message_monitor_reset(void);

// Globals
////////////////////////////////////
static const uint16_t ENDPOINT_ID = 0x30;

#define MAX_SIZE_INBOUND  32
#define MAX_SIZE_OUTBOUND 32

static const char *TEST_DATA =
    "01234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789";
static const uint32_t TEST_KEY = 0xbeefbabe;
static const uint8_t TEST_TRANSACTION_ID_1 = 0x11; // msgs with this ID are asserted to be ack'd
static const uint8_t TEST_TRANSACTION_ID_2 = 0x22; // msgs with this ID are asserted to be nack'd
static const uint16_t MAX_DATA_SIZE = MAX_SIZE_OUTBOUND - sizeof(Dictionary) - sizeof(Tuple);

static int s_context;

static DictionaryIterator s_expected_iter;
uint8_t s_expected_buffer[MAX_SIZE_OUTBOUND];

static int s_out_sent_call_count = 0;
static int s_out_failed_call_count = 0;
static AppMessageResult s_failure_result = APP_MSG_OK;
static bool s_ack_sent_is_called = false;
static bool s_nack_sent_is_called = false;
static bool s_in_received_is_called = false;
static bool s_in_dropped_is_called = false;
static bool s_ack_received_for_id_1 = false;
static bool s_nack_received_for_id_2 = false;

static AppMessageCtx s_app_message_ctx;

typedef void (*RemoteReceiveHandler)(uint16_t endpoint_id, const uint8_t *data,
                                     unsigned int length);
static RemoteReceiveHandler s_remote_receive_handler;

// UUID: 6bf6215b-c97f-409e-8c31-4f55657222b4
static Uuid simplicity_uuid = (Uuid){0x6b, 0xf6, 0x21, 0x5b, 0xc9, 0x7f, 0x40, 0x9e,
                                     0x8c, 0x31, 0x4f, 0x55, 0x65, 0x72, 0x22, 0xb4};

static struct pbl_pp_session *s_fake_app_comm_session = (struct pbl_pp_session *)0xaabbccdd;
static bool s_is_connected;
static bool s_is_app_message_receiver_open;
static Uuid s_app_uuid;
static Uuid s_remote_app_uuid;
static bool s_app_receiver_oom;

// Utils
////////////////////////////////////

static void prv_set_app_uuid(Uuid uuid) {
  s_app_uuid = uuid;
}

static void prv_set_remote_app_uuid(Uuid uuid) {
  s_remote_app_uuid = uuid;
}

//! @note Assumes same order of tuples in both dictionaries!
static void prv_assert_dict_equal(DictionaryIterator *a, DictionaryIterator *b) {
  Tuple *a_tuple = dict_read_first(a);
  Tuple *b_tuple = dict_read_first(b);
  while (b_tuple && a_tuple) {
    cl_assert_equal_i(a_tuple->key, b_tuple->key);
    cl_assert_equal_i(a_tuple->length, b_tuple->length);
    cl_assert_equal_i(a_tuple->type, b_tuple->type);
    cl_assert_equal_m(a_tuple->value, b_tuple->value, a_tuple->length);
    a_tuple = dict_read_next(a);
    b_tuple = dict_read_next(b);
  }
  if (b_tuple) {
    cl_fail("Dictionary `B` contained more tuples than dictionary `A`.");
  } else if (a_tuple) {
    cl_fail("Dictionary `A` contained more tuples than dictionary `B`.");
  }
}

// Callbacks
////////////////////////////////////

static void prv_out_sent_callback(DictionaryIterator *sent, void *context) {
  s_out_sent_call_count++;
  cl_assert_equal_p(context, &s_context);
  prv_assert_dict_equal(sent, &s_expected_iter);

  // When the outbox sent callback is called, the outbox should be in the
  // ACCEPTING state again.
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

static void prv_out_failed_callback(DictionaryIterator *failed, AppMessageResult reason,
                                    void *context) {
  s_out_failed_call_count++;
  cl_assert_equal_p(context, &s_context);
  prv_assert_dict_equal(failed, &s_expected_iter);
  s_failure_result = reason;

  // When the outbox failed callback is called, the outbox should be in the
  // ACCEPTING state again.
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

static void prv_in_received_callback(DictionaryIterator *received, void *context) {
  cl_assert_equal_p(context, &s_context);
  prv_assert_dict_equal(received, &s_expected_iter);
  s_in_received_is_called = true;
}

static void prv_in_dropped_callback(AppMessageResult reason, void *context) {
  cl_assert_equal_p(context, &s_context);
  cl_assert_equal_b(s_in_dropped_is_called, false);
  s_in_dropped_is_called = true;
}

static void prv_send_ack_nack(uint16_t endpoint_id, const uint8_t *data, unsigned int length,
                              bool nack) {
  const int o = offsetof(AppMessage, payload[0].push.dictionary);
  cl_assert_equal_i(length, o + dict_calc_buffer_size(1, (size_t)MAX_DATA_SIZE));
  struct pbl_pp_session *session = s_fake_app_comm_session;
  AppMessage *message = (AppMessage *)data;
  AppMessage ack = {
    .command = nack ? CMD_NACK : CMD_ACK,
    .transaction_id = message->transaction_id,
  };

  if (endpoint_id == ENDPOINT_ID) {
    app_message_app_protocol_msg_callback(session, (const uint8_t *)&ack, sizeof(AppMessage),
                                          nullptr);
  } else {
    cl_fail("Unhandled endpoint");
  }
}

static void prv_nack_sent_callback(uint16_t endpoint_id, const uint8_t *data, unsigned int length) {
  s_nack_sent_is_called = true;
  prv_send_ack_nack(endpoint_id, data, length, true);
}

static void prv_ack_sent_callback(uint16_t endpoint_id, const uint8_t *data, unsigned int length) {
  s_ack_sent_is_called = true;
  prv_send_ack_nack(endpoint_id, data, length, false);
}

static void prv_receive_test_data(uint8_t transaction_id) {
  const uint16_t dict_length = dict_calc_buffer_size(1, (size_t)MAX_DATA_SIZE);
  const uint16_t message_length = offsetof(AppMessage, payload[0].push.dictionary) + dict_length;
  uint8_t buffer[message_length];
  AppMessage *message = (AppMessage *)buffer;

  message->command = CMD_PUSH;
  message->transaction_id = transaction_id;
  message->payload->push.uuid = s_remote_app_uuid;
  memcpy(&message->payload->push.dictionary, s_expected_buffer, dict_length);
  PBL_LOG_DBG("message->transaction_id = %" PRIu32, message->transaction_id);

  struct pbl_pp_session *session = s_fake_app_comm_session;
  app_message_app_protocol_msg_callback(session, buffer, message_length, nullptr);
}

static void prv_receive_ack_nack_callback(uint16_t endpoint_id, const uint8_t *data,
                                          unsigned int length) {
  AppMessage *message = (AppMessage *)data;
  cl_assert(length == sizeof(AppMessage));
  PBL_LOG_DBG("message %" PRIu32 ", id1 %" PRIu32 ", id2 %" PRIu32, message->transaction_id,
              TEST_TRANSACTION_ID_1, TEST_TRANSACTION_ID_2);
  if (message->transaction_id == TEST_TRANSACTION_ID_1) {
    cl_assert_equal_b(s_ack_received_for_id_1, false);
    s_ack_received_for_id_1 = true;
    cl_assert_equal_i(message->command, CMD_ACK);
  } else if (message->transaction_id == TEST_TRANSACTION_ID_2) {
    cl_assert_equal_b(s_nack_received_for_id_2, false);
    s_nack_received_for_id_2 = true;
    cl_assert_equal_i(message->command, CMD_NACK);
  } else {
    cl_assert(false);
  }
}

static void prv_no_reply_callback(uint16_t endpoint_id, const uint8_t *data, unsigned int length) {
}

// Overrides
///////////////////////////////////

bool sys_app_pp_has_capability(enum pbl_pp_capability capability) {
  return true;
}

static int s_sys_psleep_last_millis;

void sys_psleep(int millis) {
  s_sys_psleep_last_millis = millis;
}

AppMessageCtx *app_state_get_app_message_ctx(void) {
  return &s_app_message_ctx;
}

bool app_message_receiver_open(size_t buffer_size) {
  if (s_app_receiver_oom) {
    return false;
  }
  s_is_app_message_receiver_open = true;
  return true;
}

void app_message_receiver_close(void) {
  s_is_app_message_receiver_open = false;
}

size_t sys_app_pp_app_message_inbox_size_maximum(void) {
  return 600;
}

bool sys_get_current_app_is_js_allowed(void) {
  return false;
}

Version sys_get_current_app_sdk_version(void) {
  return (Version){};
}

static uint16_t s_sent_endpoint_id;
static uint8_t *s_sent_data;
static uint16_t s_sent_data_length;

void prv_send_data(uint16_t endpoint_id, const uint8_t *data, uint16_t length) {
  const size_t header_size =
      (uintptr_t)(((AppMessage *)0)->payload[0].push.dictionary.head[0].value->data);
  const uint16_t max_length = (header_size + MAX_DATA_SIZE);
  if (length > max_length) {
    // Using cl_assert_equal_i for the nicer printing.
    // when getting at this point, it will always trip:
    cl_assert_equal_i(length, max_length);
  }

  cl_assert_equal_p(s_sent_data, nullptr);
  s_sent_data = pbl_malloc(length);
  cl_assert(s_sent_data);
  memcpy(s_sent_data, data, length);
  s_sent_data_length = length;
  s_sent_endpoint_id = endpoint_id;
}

bool sys_app_pp_send_data(struct pbl_pp_session *session, uint16_t endpoint_id, const uint8_t *data,
                          uint16_t length) {
  if (!s_is_connected) {
    return false;
  }
  prv_send_data(endpoint_id, data, length);
  return true;
}

static AppOutboxSentHandler s_app_outbox_sent_handler;
static void *s_app_outbox_ctx;

static void prv_call_outbox_sent(int status) {
  cl_assert(s_app_outbox_sent_handler);
  s_app_outbox_sent_handler(status, s_app_outbox_ctx);
}

void app_outbox_send(const uint8_t *data, size_t length, AppOutboxSentHandler sent_handler,
                     void *cb_ctx) {
  if (!s_is_connected) {
    sent_handler(AppOutboxStatusConsumerDoesNotExist, cb_ctx);
    return;
  }
  s_app_outbox_sent_handler = sent_handler;
  s_app_outbox_ctx = cb_ctx;
  AppMessageAppOutboxData *outbox_data = (AppMessageAppOutboxData *)data;
  prv_send_data(outbox_data->endpoint_id, outbox_data->payload,
                length - sizeof(AppMessageAppOutboxData));
}

static void prv_process_sent_data(void) {
  if (!s_sent_data) {
    return;
  }
  if (!s_is_connected) {
    return;
  }
  if (!s_is_app_message_receiver_open) {
    return;
  }
  cl_assert(s_remote_receive_handler);
  s_remote_receive_handler(s_sent_endpoint_id, s_sent_data, s_sent_data_length);
  pbl_free(s_sent_data);
  s_sent_data = nullptr;
}

void sys_get_app_uuid(Uuid *uuid) {
  cl_assert(uuid);
  *uuid = s_app_uuid;
}

static void (*s_process_manager_callback)(void *data);
static void *s_process_manager_callback_data;
void sys_current_process_schedule_callback(CallbackEventCallback async_cb, void *ctx) {
  // Expecting the stub to be called only once durning a test:
  cl_assert_equal_p(s_process_manager_callback, nullptr);
  cl_assert_equal_p(s_process_manager_callback_data, nullptr);

  s_process_manager_callback = async_cb;
  s_process_manager_callback_data = ctx;
}

static int s_phone_prefs_call_count;
static uint16_t s_phone_prefs_dict_size;
void sys_app_message_handle_phone_prefs(const uint8_t *dict, uint16_t dict_size) {
  ++s_phone_prefs_call_count;
  s_phone_prefs_dict_size = dict_size;
}

static int s_app_inbox_consume_call_count;
void app_inbox_consume(AppInboxConsumerInfo *consumer_info) {
  ++s_app_inbox_consume_call_count;
}

// Setup
////////////////////////////////////
void test_app_message__initialize(void) {
  prv_set_app_uuid(simplicity_uuid);
  prv_set_remote_app_uuid(simplicity_uuid);

  fake_app_timer_init();

  s_app_receiver_oom = false;

  s_sys_psleep_last_millis = 0;
  s_app_inbox_consume_call_count = 0;
  s_phone_prefs_call_count = 0;

  app_message_init();
  app_message_set_context(&s_context);
  cl_assert_equal_i(app_message_open(MAX_SIZE_INBOUND, MAX_SIZE_OUTBOUND), APP_MSG_OK);
  cl_assert_equal_p(app_message_register_outbox_sent(prv_out_sent_callback), nullptr);
  cl_assert_equal_p(app_message_register_outbox_failed(prv_out_failed_callback), nullptr);
  cl_assert_equal_p(app_message_register_inbox_dropped(prv_in_dropped_callback), nullptr);
  cl_assert_equal_p(app_message_register_inbox_received(prv_in_received_callback), nullptr);

  s_out_sent_call_count = 0;
  s_out_failed_call_count = 0;
  s_ack_sent_is_called = false;
  s_nack_sent_is_called = false;
  s_in_received_is_called = false;
  s_in_dropped_is_called = false;
  s_ack_received_for_id_1 = false;
  s_nack_received_for_id_2 = false;
  s_remote_receive_handler = nullptr;
  s_failure_result = APP_MSG_OK;

  s_process_manager_callback = nullptr;
  s_process_manager_callback_data = nullptr;

  s_is_connected = true;

  // Create the dictionary that is used to compare with what has been received:
  dict_write_begin(&s_expected_iter, s_expected_buffer, MAX_SIZE_OUTBOUND);
  cl_assert_equal_i(DICT_OK, dict_write_data(&s_expected_iter, TEST_KEY, (const uint8_t *)TEST_DATA,
                                             MAX_DATA_SIZE));
  dict_write_end(&s_expected_iter);
}

void test_app_message__cleanup(void) {
  app_message_close();
  cl_assert_equal_b(app_message_is_closed_inbound(), true);
  cl_assert_equal_b(app_message_is_closed_outbound(), true);
  fake_app_timer_deinit();
  pbl_free(s_sent_data);
  s_sent_data = nullptr;
}

// Test OUTBOUND (watch->phone):
////////////////////////////////////

static void prv_send_test_data_expecting_result(AppMessageResult result) {
  DictionaryIterator *iter;
  cl_assert_equal_i(app_message_outbox_begin(&iter), APP_MSG_OK);
  cl_assert_equal_i(dict_write_data(iter, TEST_KEY, (const uint8_t *)TEST_DATA, MAX_DATA_SIZE),
                    DICT_OK);
  cl_assert_equal_i(app_message_outbox_send(), result);
}

static void prv_send_test_data(void) {
  prv_send_test_data_expecting_result(APP_MSG_OK);
}

static void prv_set_remote_receive_handler(RemoteReceiveHandler handler) {
  s_remote_receive_handler = handler;
}

void test_app_message__send_happy_case_outbox_sent_then_ack(void) {
  prv_set_remote_receive_handler(prv_ack_sent_callback);
  prv_send_test_data();
  prv_call_outbox_sent(AppOutboxStatusSuccess);
  prv_process_sent_data();

  // After the ACK has been received, we should have been called
  cl_assert_equal_b(s_ack_sent_is_called, true);

  // Since that callback schedules another callback, we have to invoke
  // system tasks again to get th actual callback to trigger.
  cl_assert_equal_i(s_out_sent_call_count, 1);

  // Check that the state is reset properly after everything
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__send_happy_case_ack_then_outbox_sent(void) {
  prv_set_remote_receive_handler(prv_ack_sent_callback);
  prv_send_test_data();
  prv_process_sent_data();

  // With certain PP transports (i.e. PPoGATT), the 'consuming' of the outbound data / outbox sent
  // callback can fire after the AppMessage (N)ACK has been received.
  cl_assert_equal_b(app_message_is_accepting_outbound(), false);
  prv_call_outbox_sent(AppOutboxStatusSuccess);

  // After the ACK has been received, we should have been called
  cl_assert_equal_b(s_ack_sent_is_called, true);

  // Since that callback schedules another callback, we have to invoke
  // system tasks again to get th actual callback to trigger.
  cl_assert_equal_i(s_out_sent_call_count, 1);

  // Check that the state is reset properly after everything
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__cancel_timer(void) {
  prv_set_remote_receive_handler(prv_ack_sent_callback);
  prv_send_test_data();
  prv_call_outbox_sent(AppOutboxStatusSuccess);
  prv_process_sent_data();

  // After the ACK has been received, we should have been called
  cl_assert_equal_b(s_ack_sent_is_called, true);

  // Check that we were called
  cl_assert_equal_i(s_out_sent_call_count, 1);

  // Timer should be invalid
  cl_assert_equal_b(!fake_app_timer_is_scheduled(app_message_ack_timer_id()), true);

  // Check the state is reset properly
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__send_ack_timeout(void) {
  // We'll send the ack right after the timeout
  prv_set_remote_receive_handler(prv_ack_sent_callback);
  prv_send_test_data();
  prv_call_outbox_sent(AppOutboxStatusSuccess);

  // Fire the timeout and send the data
  app_timer_trigger(app_message_ack_timer_id());
  prv_process_sent_data();

  cl_assert_equal_i(s_out_sent_call_count, 0);
  cl_assert_equal_i(s_out_failed_call_count, 1);
  cl_assert_equal_i(s_failure_result, APP_MSG_SEND_TIMEOUT);

  // Check the state is reset properly
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__send_rejected(void) {
  // Sending ack on timeout, but reject the send
  prv_set_remote_receive_handler(prv_nack_sent_callback);
  prv_send_test_data();
  prv_call_outbox_sent(AppOutboxStatusSuccess);
  prv_process_sent_data();

  // Fire the ack timeout after receiving the nack
  app_timer_trigger(app_message_ack_timer_id());
  cl_assert_equal_b(s_nack_sent_is_called, true);

  cl_assert_equal_i(s_out_sent_call_count, 0);
  cl_assert_equal_i(s_out_failed_call_count, 1);
  cl_assert_equal_i(s_failure_result, APP_MSG_SEND_REJECTED);

  // Check the state is reset properly
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__nack_then_outbox_sent(void) {
  // Sending ack on timeout, but reject the send
  prv_set_remote_receive_handler(prv_nack_sent_callback);
  prv_send_test_data();
  prv_process_sent_data();

  cl_assert_equal_b(app_message_is_accepting_outbound(), false);
  prv_call_outbox_sent(AppOutboxStatusSuccess);

  cl_assert_equal_b(s_nack_sent_is_called, true);

  cl_assert_equal_i(s_out_sent_call_count, 0);
  cl_assert_equal_i(s_out_failed_call_count, 1);
  cl_assert_equal_i(s_failure_result, APP_MSG_SEND_REJECTED);

  // Check the state is reset properly
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__busy(void) {
  DictionaryIterator *iter;
  prv_set_remote_receive_handler(prv_no_reply_callback);
  prv_send_test_data();
  prv_process_sent_data();

  // Can't get or send again if still sending
  cl_assert_equal_i(app_message_outbox_begin(&iter), APP_MSG_BUSY);
  cl_assert_equal_i(app_message_outbox_send(), APP_MSG_BUSY);

  // Can't get or send again if waiting on the ACK
  cl_assert_equal_i(app_message_outbox_begin(&iter), APP_MSG_BUSY);
  cl_assert_equal_i(app_message_outbox_send(), APP_MSG_BUSY);
}

void test_app_message__send_disconnected(void) {
  prv_set_remote_receive_handler(prv_nack_sent_callback);

  // Disconnect the comm session
  s_is_connected = false;

  // The return value should be APP_MSG_OK, even though we already know it's going to fail.
  // The failure should be delivered after returning from app_message_outbox_send(), because
  // some apps call .._send() again from the failed_callback.
  prv_send_test_data_expecting_result(APP_MSG_OK);

  // Make fake remote send any outstanding data (none expected)
  prv_process_sent_data();

  cl_assert_equal_i(s_out_sent_call_count, 0);
  // failed_callback not called yet:
  cl_assert_equal_i(s_out_failed_call_count, 0);

  // Now process the scheduled callback event:
  cl_assert(s_process_manager_callback);
  s_process_manager_callback(s_process_manager_callback_data);

  // Check that the ack/nack timer is removed:
  cl_assert_equal_p(app_message_outbox_get_ack_nack_timer(), nullptr);

  cl_assert_equal_i(1, s_out_failed_call_count);
  cl_assert_equal_i(s_failure_result, APP_MSG_NOT_CONNECTED);
  cl_assert_equal_b(s_nack_sent_is_called, false);

  // Check the state is reset properly
  cl_assert_equal_b(app_message_is_accepting_outbound(), true);
}

void test_app_message__send_while_closing_and_while_being_disconnected(void) {
  prv_set_remote_receive_handler(prv_nack_sent_callback);
  prv_send_test_data();

  // Disconnect the comm session and remove the
  // app message context
  s_is_connected = false;
  app_message_close();

  // Make fake remote send any outstanding data (none expected)
  prv_process_sent_data();

  // No app_message callbacks are expected to be called, as we closed the context
  cl_assert_equal_i(s_out_sent_call_count, 0);
  cl_assert_equal_b(s_nack_sent_is_called, false);
  cl_assert_equal_i(s_out_failed_call_count, 0);
  cl_assert_equal_b(app_message_is_closed_outbound(), true);
}

void test_app_message__send_while_closing(void) {
  prv_set_remote_receive_handler(prv_ack_sent_callback);
  prv_send_test_data();

  // Close the AppMessage context
  app_message_close();

  // Make fake remote send the ack if something has been sent (not expected)
  prv_process_sent_data();

  // Test that timer has been invalidated
  cl_assert_equal_b(!fake_app_timer_is_scheduled(app_message_ack_timer_id()), true);
  cl_assert_equal_b(s_ack_sent_is_called, false);

  cl_assert_equal_i(s_out_sent_call_count, 0);
  cl_assert_equal_i(s_out_failed_call_count, 0);
  cl_assert_equal_b(app_message_is_closed_outbound(), true);
  cl_assert_equal_b(app_message_is_closed_inbound(), true);
}

void test_app_message__throttle_repeated_outbox_begin_calls(void) {
  prv_set_remote_receive_handler(prv_no_reply_callback);
  prv_send_test_data();

  // Expect exponential back-off:
  for (int i = 1; i <= 128; i *= 2) {
    DictionaryIterator *iter;
    cl_assert_equal_i(app_message_outbox_begin(&iter), APP_MSG_BUSY);
    cl_assert_equal_i(MIN(i, 100), s_sys_psleep_last_millis);
  }
}

// Test INBOUND (phone->watch):
////////////////////////////////////
static void check_in_accepting_again(void) {
  cl_assert(app_message_is_accepting_inbound() == true);
}

void test_app_message__receive_happy_case(void) {
  prv_set_remote_receive_handler(prv_receive_ack_nack_callback);
  prv_receive_test_data(TEST_TRANSACTION_ID_1);
  cl_assert_equal_i(s_app_inbox_consume_call_count, 1);
  prv_process_sent_data();

  // First Message
  cl_assert(s_in_received_is_called == true);

  // ACK the messages

  // Check that state was reset properly
  check_in_accepting_again();
}

void test_app_message__receive_app_uuid_mismatch(void) {
  // Change the current app uuid
  prv_set_app_uuid(UuidMake(0xF6, 0x2C, 0xB7, 0xBA, 0x1B, 0x8D, 0x46, 0x10, 0xBE, 0xC5, 0xDE, 0xC6,
                            0x5A, 0xD3, 0x18, 0x29));

  prv_set_remote_receive_handler(prv_receive_ack_nack_callback);
  prv_receive_test_data(TEST_TRANSACTION_ID_2);
  prv_process_sent_data();

  cl_assert_equal_b(s_in_received_is_called, false);
  cl_assert_equal_b(s_in_dropped_is_called, false);

  cl_assert_equal_b(s_nack_received_for_id_2, true);

  // Check that the state is reset
  check_in_accepting_again();
}

void test_app_message__get_context(void) {
  cl_assert_equal_p(app_message_get_context(), &s_context);
}

void test_app_message__open_while_already_open(void) {
  cl_assert_equal_i(app_message_open(MAX_SIZE_INBOUND, MAX_SIZE_OUTBOUND), APP_MSG_INVALID_STATE);
}

void test_app_message__begin_while_already_begun(void) {
  DictionaryIterator *iterator;
  cl_assert_equal_i(app_message_outbox_begin(&iterator), APP_MSG_OK);
  cl_assert_equal_i(app_message_outbox_begin(&iterator), APP_MSG_INVALID_STATE);
}

void test_app_message__begin_null_iterator(void) {
  cl_assert_equal_i(app_message_outbox_begin(nullptr), APP_MSG_INVALID_ARGS);
}

void test_app_message__send_while_not_begun(void) {
  cl_assert_equal_i(app_message_outbox_send(), APP_MSG_INVALID_STATE);
}

void test_app_message__zero_inbox(void) {
  app_message_close();
  cl_assert_equal_i(app_message_open(0, MAX_SIZE_OUTBOUND), APP_MSG_OK);
  cl_assert_equal_b(app_message_is_closed_inbound(), true);
  cl_assert_equal_b(app_message_is_closed_outbound(), false);
}

void test_app_message__zero_outbox(void) {
  app_message_close();
  cl_assert_equal_i(app_message_open(MAX_SIZE_INBOUND, 0), APP_MSG_OK);
  cl_assert_equal_b(app_message_is_closed_inbound(), false);
  cl_assert_equal_b(app_message_is_closed_outbound(), true);

  DictionaryIterator *iterator;
  cl_assert_equal_i(app_message_outbox_begin(&iterator), APP_MSG_INVALID_STATE);
}

void test_app_message__oom(void) {
  s_app_receiver_oom = true;
  app_message_close();
  cl_assert_equal_i(app_message_open(MAX_SIZE_INBOUND, MAX_SIZE_OUTBOUND), APP_MSG_OUT_OF_MEMORY);
  cl_assert_equal_b(app_message_is_closed_inbound(), true);
  cl_assert_equal_b(app_message_is_closed_outbound(), true);
}

void test_app_message__kernel_nack_handler(void) {
  prv_set_remote_receive_handler(prv_receive_ack_nack_callback);

  const AppMessagePush push = {
    .header = {
      .command = CMD_PUSH,
      .transaction_id = TEST_TRANSACTION_ID_2,
    },
  };
  app_message_app_protocol_system_nack_callback(s_fake_app_comm_session, (const uint8_t *)&push,
                                                sizeof(push));

  prv_process_sent_data();
  cl_assert_equal_b(s_nack_received_for_id_2, true);
}

// Watch settings keys (Graphite):
////////////////////////////////////

static const Uuid s_graphite_uuid = APP_MESSAGE_PHONE_PREFS_APP_UUID;
static const Uuid s_target_uuid = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                   0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
static int s_phone_prefs_received_tuples;

static void prv_phone_prefs_received_callback(DictionaryIterator *received, void *context) {
  s_phone_prefs_received_tuples = 0;
  for (Tuple *t = dict_read_first(received); t; t = dict_read_next(received)) {
    ++s_phone_prefs_received_tuples;
  }
  s_in_received_is_called = true;
}

//! Receives an ACK'd message with an app key and, if @p reserved_key isn't 0, that key.
static uint32_t prv_receive_phone_prefs_message(uint32_t reserved_key) {
  uint8_t dict_buffer[128];
  DictionaryIterator iter;
  cl_assert_equal_i(dict_write_begin(&iter, dict_buffer, sizeof(dict_buffer)), DICT_OK);
  cl_assert_equal_i(dict_write_uint32(&iter, TEST_KEY, 7), DICT_OK);
  if (reserved_key) {
    cl_assert_equal_i(dict_write_data(&iter, reserved_key, (const uint8_t *)&s_target_uuid,
                                      sizeof(Uuid)),
                      DICT_OK);
  }
  const uint32_t dict_length = dict_write_end(&iter);

  const uint16_t message_length = offsetof(AppMessage, payload[0].push.dictionary) + dict_length;
  uint8_t buffer[message_length];
  AppMessage *message = (AppMessage *)buffer;
  message->command = CMD_PUSH;
  message->transaction_id = TEST_TRANSACTION_ID_1;
  message->payload->push.uuid = s_remote_app_uuid;
  memcpy(&message->payload->push.dictionary, dict_buffer, dict_length);

  app_message_register_inbox_received(prv_phone_prefs_received_callback);
  prv_set_remote_receive_handler(prv_receive_ack_nack_callback);
  app_message_app_protocol_msg_callback(s_fake_app_comm_session, buffer, message_length, nullptr);
  prv_process_sent_data();
  return dict_length;
}

void test_app_message__phone_prefs_keys_from_graphite(void) {
  prv_set_app_uuid(s_graphite_uuid);
  prv_set_remote_app_uuid(s_graphite_uuid);
  const uint32_t keys[] = {APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST, APP_MESSAGE_KEY_SETTING_FIRST,
                           APP_MESSAGE_KEY_SETTING_LAST, APP_MESSAGE_KEY_SETTINGS_REQUEST};
  for (size_t i = 0; i < ARRAY_LENGTH(keys); i++) {
    s_phone_prefs_call_count = 0;
    s_ack_received_for_id_1 = false;
    const uint32_t dict_length = prv_receive_phone_prefs_message(keys[i]);

    // The whole dictionary goes to the kernel once, and the app still gets every key:
    cl_assert_equal_i(s_phone_prefs_call_count, 1);
    cl_assert_equal_i(s_phone_prefs_dict_size, dict_length);
    cl_assert_equal_i(s_phone_prefs_received_tuples, 2);
    cl_assert_equal_b(s_ack_received_for_id_1, true);
  }
}

void test_app_message__phone_prefs_no_reserved_keys(void) {
  prv_set_app_uuid(s_graphite_uuid);
  prv_set_remote_app_uuid(s_graphite_uuid);
  prv_receive_phone_prefs_message(0);
  s_ack_received_for_id_1 = false;
  prv_receive_phone_prefs_message(APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST - 1);
  cl_assert_equal_i(s_phone_prefs_call_count, 0);
  cl_assert_equal_i(s_phone_prefs_received_tuples, 2);
}

void test_app_message__phone_prefs_ignored_for_other_apps(void) {
  prv_receive_phone_prefs_message(APP_MESSAGE_KEY_SETTING_FIRST);
  cl_assert_equal_i(s_phone_prefs_call_count, 0);
  cl_assert_equal_b(s_in_received_is_called, true);
  cl_assert_equal_b(s_ack_received_for_id_1, true);
}

static uint8_t s_last_sent_transaction_id;
static void prv_record_transaction_id_callback(uint16_t endpoint_id, const uint8_t *data,
                                               unsigned int length) {
  s_last_sent_transaction_id = ((const AppMessage *)data)->transaction_id;
  prv_send_ack_nack(endpoint_id, data, length, false);
}

void test_app_message__outbox_skips_kernel_transaction_ids(void) {
  prv_set_remote_receive_handler(prv_record_transaction_id_callback);
  s_app_message_ctx.outbox.transaction_id = APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST - 2;
  const uint8_t expected[] = {APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST - 1, 0, 1};
  for (size_t i = 0; i < ARRAY_LENGTH(expected); i++) {
    prv_send_test_data();
    prv_call_outbox_sent(AppOutboxStatusSuccess);
    prv_process_sent_data();
    cl_assert_equal_i(s_last_sent_transaction_id, expected[i]);
  }
  cl_assert_equal_i(s_out_sent_call_count, ARRAY_LENGTH(expected));
}

void test_app_message__kernel_transaction_ack_does_not_reach_outbox(void) {
  // An app message awaits its ACK:
  prv_set_remote_receive_handler(prv_no_reply_callback);
  prv_send_test_data();
  prv_call_outbox_sent(AppOutboxStatusSuccess);
  prv_process_sent_data();
  cl_assert_equal_b(app_message_is_accepting_outbound(), false);

  // A NACK for a message the kernel sent with a reserved id is not taken for the app's:
  s_app_message_ctx.outbox.transaction_id = APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST;
  const AppMessageHeader nack = {
    .command = CMD_NACK,
    .transaction_id = APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST,
  };
  app_message_app_protocol_msg_callback(s_fake_app_comm_session, (const uint8_t *)&nack,
                                        sizeof(nack), nullptr);
  cl_assert_equal_b(app_message_is_accepting_outbound(), false);
  cl_assert_equal_i(s_out_failed_call_count, 0);
  cl_assert_equal_i(s_out_sent_call_count, 0);
}
