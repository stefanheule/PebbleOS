/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/kernel/compiler.h>
#include <pbl/services/app_message/app_message_sender.h>
#include <pbl/util/uuid.h>

#include <applib/app_message/app_message.h>
#include <applib/app_timer.h>

struct pbl_pp_session;

#define ACK_NACK_TIME_OUT_MS    (10000)
#define APP_MESSAGE_ENDPOINT_ID (0x30)

typedef enum {
  CMD_PUSH = 0x01,
  CMD_REQUEST = 0x02,
  CMD_ACK = 0xff,
  CMD_NACK = 0x7f,
} AppMessageCmd;

typedef struct PBL_PACKED {
  AppMessageCmd command : 8;
  uint8_t transaction_id;
} AppMessageHeader;

//! The actual wire format of an app message message
typedef struct PBL_PACKED {
  AppMessageHeader header;
  Uuid uuid;
  Dictionary dictionary; //!< Variable length!
} AppMessagePush;

//! Graphite, the watchface through whose AppMessages the phone reads and writes the watch's
//! settings: 7e5267ac-798f-4953-a645-1b87a5c29d96. Messages to other apps are left alone.
#define APP_MESSAGE_PHONE_PREFS_APP_UUID                                                      \
  UuidMake(0x7e, 0x52, 0x67, 0xac, 0x79, 0x8f, 0x49, 0x53, 0xa6, 0x45, 0x1b, 0x87, 0xa5, 0xc2, \
           0x9d, 0x96)
//! Reserved AppMessage keys; they still reach the app's inbox like any other key.
//! Phone to watch: the quick launch apps, one per QuickLaunchSlot from FIRST to LAST, each a
//! 16-byte app UUID, or UUID_INVALID to unassign.
#define APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST (0x57505201)
#define APP_MESSAGE_KEY_QUICK_LAUNCH_LAST  (0x57505208)
//! Phone to watch: synced settings, each a `[u8 name length][name][value]` record.
#define APP_MESSAGE_KEY_SETTING_FIRST (0x57505300)
#define APP_MESSAGE_KEY_SETTING_LAST  (0x5750533f)
//! Phone to watch: asks for all synced settings.
#define APP_MESSAGE_KEY_SETTINGS_REQUEST (0x57505400)
//! Watch to phone: the synced settings, records from FIRST on, split over messages that each
//! carry the int32 DUMP_ID; the last one also carries the int32 record COUNT.
#define APP_MESSAGE_KEY_SETTINGS_DUMP_FIRST (0x57505401)
#define APP_MESSAGE_KEY_SETTINGS_DUMP_ID    (0x575054fe)
#define APP_MESSAGE_KEY_SETTINGS_DUMP_COUNT (0x575054ff)
//! The whole reserved range, for a quick test.
#define APP_MESSAGE_KEY_PHONE_PREFS_FIRST APP_MESSAGE_KEY_QUICK_LAUNCH_FIRST
#define APP_MESSAGE_KEY_PHONE_PREFS_LAST  APP_MESSAGE_KEY_SETTINGS_DUMP_COUNT

//! Transaction ids of the messages the kernel sends for an app. An app's outbox skips them, and
//! (n)acks for them never reach it.
#define APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST (0xf8)
#define APP_MESSAGE_KERNEL_TRANSACTION_IDS      (0x100 - APP_MESSAGE_KERNEL_TRANSACTION_ID_FIRST)

// AppMessageHeader and Uuid size should be opaque to user of API
#define APP_MSG_HDR_OVRHD_SIZE (offsetof(AppMessagePush, dictionary))

#define APP_MSG_8K_DICT_SIZE (sizeof(Dictionary) + sizeof(Tuple) + (8 * 1024))

typedef struct PBL_PACKED {
  AppMessageHeader header;
} AppMessageAck;

typedef enum AppMessagePhaseOut {
  //! The App Message Outbox is not enabled.  To enable it, call app_message_open().
  OUT_CLOSED = 0,
  //! The dictionary writing can be "started" by calling app_message_outbox_begin()
  OUT_ACCEPTING,
  //! app_message_outbox_begin() has been called and the dictionary can be written and then sent.
  OUT_WRITING,
  //! app_message_outbox_send() has been called. The ack/nack timeout timer has been set and
  //! we're awaiting an ack/nack on the sent message AND the callback from the AppOutbox subsystem
  //! that the data has been consumed. These 2 things happen in parallel, the order in which they
  //! happen is undefined.
  OUT_AWAITING_REPLY_AND_OUTBOX_CALLBACK,
  //! We're still awaiting the AppMessage ack/nack, but the AppOutbox subsystem has indicated that
  //! the data has been consumed.
  OUT_AWAITING_REPLY,
  //! We're still awaiting the callback from the AppOutbox subsystem to indicate the data has been
  //! consumed, but we have already received the AppMessage ack/nack. This state is possible because
  //! acking at a lower layer (i.e. PPoGATT) can happen with a slight delay.
  OUT_AWAITING_OUTBOX_CALLBACK,
} AppMessagePhaseOut;

typedef struct AppMessageCtxInbox {
  bool is_open;
  void *user_context;
  AppMessageInboxReceived received_callback;
  AppMessageInboxDropped dropped_callback;
} AppMessageCtxInbox;

typedef struct AppMessageCtxOutbox {
  DictionaryIterator iterator;
  size_t transmission_size_limit;

  AppMessageAppOutboxData *app_outbox_message;

  AppMessageOutboxSent sent_callback;
  AppMessageOutboxFailed failed_callback;
  void *user_context;

  AppTimer *ack_nack_timer;

  struct PBL_PACKED {
    AppMessagePhaseOut phase : 8;
    uint8_t transaction_id;
    uint16_t not_ready_throttle_ms; // used for throttling app task when outbox is not ready
    AppMessageResult result : 16;
  };
} AppMessageCtxOutbox;

typedef struct AppMessageCtx {
  AppMessageCtxInbox inbox;
  AppMessageCtxOutbox outbox;
} AppMessageCtx;

static_assert(sizeof(AppMessageCtx) <= 112, "AppMessageCtx must not exceed 112 bytes!");

typedef struct {
  struct pbl_pp_session *session;
  //! To give us some room for future changes. This structure ends up in a buffer that is sized by
  //! the app, so we can't easily increase the size of this once shipped.
  uint8_t padding[8];
  uint8_t data[];
} AppMessageReceiverHeader;

#if !UNITTEST && __SIZEOF_POINTER__ == 4
static_assert(sizeof(AppMessageReceiverHeader) == 12,
              "The size of AppMessageReceiverHeader cannot grow beyond 12 bytes!");
#endif

void app_message_init(void);

AppMessageResult app_message_inbox_open(AppMessageCtxInbox *inbox, size_t size_inbound);

void app_message_inbox_close(AppMessageCtxInbox *inbox);

typedef struct AppInboxConsumerInfo AppInboxConsumerInfo;

void app_message_inbox_receive(struct pbl_pp_session *session, AppMessagePush *push_message,
                               size_t length, AppInboxConsumerInfo *consumer_info);

AppMessageResult app_message_outbox_open(AppMessageCtxOutbox *outbox, size_t size_outbound);

void app_message_outbox_close(AppMessageCtxOutbox *outbox);

void app_message_out_handle_ack_nack_received(const AppMessageHeader *header);

void app_message_inbox_send_ack_nack_reply(struct pbl_pp_session *session,
                                           const uint8_t transaction_id, AppMessageCmd cmd);

void app_message_inbox_handle_dropped_messages(uint32_t num_drops);

void app_message_app_protocol_msg_callback(struct pbl_pp_session *session, const uint8_t *data,
                                           size_t length, AppInboxConsumerInfo *consumer_info);

void app_message_app_protocol_system_nack_callback(struct pbl_pp_session *session,
                                                   const uint8_t *data, size_t length);