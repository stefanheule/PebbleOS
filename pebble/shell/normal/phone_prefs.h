/* SPDX-FileCopyrightText: 2026 Stefan Heule */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
