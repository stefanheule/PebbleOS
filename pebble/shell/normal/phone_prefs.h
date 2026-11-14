/* SPDX-FileCopyrightText: 2026 Stefan Heule */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <pbl/kernel/compiler.h>
#include <pbl/util/uuid.h>

#include <process_management/pebble_process_info.h>

//! @file phone_prefs.h
//! Watch settings the phone reads and writes through the Graphite watchface's AppMessages; see
//! the APP_MESSAGE_KEY_* keys in app_message_internal.h.

//! Takes the reserved keys of a message to the running Graphite and applies them on KernelBG,
//! then sends the settings to the phone if that changed any or the message asked for them.
//! @param dict the received dictionary; only its reserved tuples are copied
//! @param dict_size its size
void phone_prefs_handle_app_message(const uint8_t *dict, uint16_t dict_size);

//! Applies one `[u8 name length][name][value]` record as a change made on the watch.
//! @return true if the setting changed
bool phone_prefs_apply_record(const uint8_t *record, size_t length);

//! Sends the phone every synced setting as `[u8 name length][name][value]` records, split over
//! as many AppMessages from Graphite as needed. Does nothing unless Graphite is running.
void phone_prefs_send_dump(void);

//! The name of an installed app's dump record is this prefix and its lowercase UUID with dashes.
#define PHONE_PREFS_APP_RECORD_PREFIX "app/"
//! An installed app's record value is `[u8 major][u8 minor][u8 flags]`, with these flags.
#define PHONE_PREFS_APP_FLAG_WATCHFACE (1 << 0)
//! Delay before a dump that waits for Graphite, so the phone has seen it start.
#define PHONE_PREFS_CHECK_DELAY_MS 3000

//! An installed third-party app, as reported in a dump.
typedef struct PBL_PACKED {
  Uuid uuid;
  //! Its version, from appinfo.json's versionLabel as the phone installed it
  Version version;
  uint8_t flags;
} PhonePrefsApp;

//! Writes the dump record of an installed app.
//! @param record room for 1 + 40 + 3 bytes
//! @return the record's length
size_t phone_prefs_app_record(uint8_t *record, const PhonePrefsApp *app);

//! Starts watching the installed apps: a change is reported in a dump as soon as Graphite runs
//! with a phone connection.
void phone_prefs_init(void);

//! Tells the phone prefs that the phone was told @p uuid runs, so a pending dump can go out.
void phone_prefs_handle_app_running(const Uuid *uuid);
