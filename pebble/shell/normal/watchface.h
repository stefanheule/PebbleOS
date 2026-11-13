/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/services/compositor/compositor.h>

#include <kernel/events.h>
#include <process_management/app_install_manager.h>

void watchface_init(void);

//! Runs the watchface's kernel-side button handling for a button event.
//! @return true if the event belongs to the app task: a press of Up, Select or
//! Down that the watchface claimed at button-down, because its top window has a
//! click handler for the button or it pushed a window over its main one.
bool watchface_handle_button_event(PebbleEvent *e);

void watchface_set_default_install_id(AppInstallId id);

AppInstallId watchface_get_default_install_id(void);

void watchface_launch_default(const CompositorTransition *animation);

void watchface_start_low_power(void);

void watchface_reset_click_manager(void);
