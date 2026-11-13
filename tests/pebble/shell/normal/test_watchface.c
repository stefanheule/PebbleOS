/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <clar.h>
#include <stdlib.h>

#include <pbl/services/compositor/default/compositor_peek_transitions.h>
#include <pbl/util/size.h>

#include <applib/ui/click_internal.h>
#include <applib/ui/window.h>
#include <apps/system/settings/quick_launch_setup_menu.h>
#include <apps/system_app_ids.h>
#include <kernel/events.h>
#include <popups/timeline/peek.h>
#include <process_management/app_manager.h>
#include <shell/normal/watchface.h>

#include <fake_app_timer.h>

// Fakes
/////////////////////////////////////////////////////////////////////////

// The running watchface: its window stack and the click manager its focused
// top window configured.
static Window s_main_window;
static Window s_menu_window;
static Window *s_top_window;
static uint32_t s_window_count;
static ClickManager s_app_click_manager;
static ProcessContext s_app_task_context;

Window *app_window_stack_get_top_window(void) {
  return s_top_window;
}

uint32_t app_window_stack_count(void) {
  return s_window_count;
}

ClickManager *app_state_get_click_manager(void) {
  return &s_app_click_manager;
}

ProcessContext *app_manager_get_task_context(void) {
  return &s_app_task_context;
}

bool low_power_is_active(void) {
  return false;
}

static int s_launch_count;
static AppInstallId s_last_launch_id;
void app_manager_put_launch_app_event(const AppLaunchEventConfig *config) {
  s_launch_count++;
  s_last_launch_id = config->id;
}

// Events the kernel synthesized for the app task.
static int s_put_count;
static PebbleEvent s_put_events[4];
void event_put(PebbleEvent *event) {
  if (s_put_count < (int)ARRAY_LENGTH(s_put_events)) {
    s_put_events[s_put_count] = *event;
  }
  s_put_count++;
}

static int s_peek_dismiss_count;
void timeline_peek_dismiss(void) {
  s_peek_dismiss_count++;
}

#define QUICK_LAUNCH_APP_ID     ((AppInstallId)42)
#define SINGLE_CLICK_APP_ID     ((AppInstallId)43)
#define COMBO_APP_ID            ((AppInstallId)44)

bool quick_launch_is_enabled(ButtonId button) {
  return true;
}

AppInstallId quick_launch_get_app(ButtonId button) {
  return QUICK_LAUNCH_APP_ID;
}

bool quick_launch_single_click_is_enabled(ButtonId button) {
  return true;
}

AppInstallId quick_launch_single_click_get_app(ButtonId button) {
  return SINGLE_CLICK_APP_ID;
}

bool quick_launch_combo_back_up_is_enabled(void) {
  return true;
}

AppInstallId quick_launch_combo_back_up_get_app(void) {
  return COMBO_APP_ID;
}

bool quick_launch_combo_up_down_is_enabled(void) {
  return false;
}

// Not reached: the quick launch apps above are set and are not the timeline.
const PebbleProcessMd *quick_launch_setup_get_app_info(void) {
  return nullptr;
}

void timeline_peek_get_item_id(TimelineItemId *item_id_out) {
}

bool timeline_peek_is_first_event(void) {
  return true;
}

bool timeline_peek_is_future_empty(void) {
  return true;
}

const CompositorTransition *compositor_peek_transition_timeline_get(void) {
  return nullptr;
}

void app_manager_set_minimum_run_level(ProcessAppRunLevel run_level) {
}

// Helpers
/////////////////////////////////////////////////////////////////////////

static void prv_handler(ClickRecognizerRef recognizer, void *context) {
}

static int s_app_click_count;
static void prv_count_app_click(ClickRecognizerRef recognizer, void *context) {
  s_app_click_count++;
}

static bool prv_press(ButtonId button_id) {
  PebbleEvent e = {.type = PEBBLE_BUTTON_DOWN_EVENT, .button.button_id = button_id};
  return watchface_handle_button_event(&e);
}

static bool prv_release(ButtonId button_id) {
  PebbleEvent e = {.type = PEBBLE_BUTTON_UP_EVENT, .button.button_id = button_id};
  return watchface_handle_button_event(&e);
}

// What the app task's event loop does with a button event it was given.
static void prv_deliver_to_app(PebbleEventType type, ButtonId button_id) {
  ClickRecognizer *recognizer = &s_app_click_manager.recognizers[button_id];
  if (type == PEBBLE_BUTTON_DOWN_EVENT) {
    click_recognizer_handle_button_down(recognizer);
  } else {
    click_recognizer_handle_button_up(recognizer);
  }
}

// Fires the earliest-registered pending timer, as time passing would.
static bool prv_fire_oldest_timer(void) {
  FakeAppTimer *oldest = nullptr;
  for (FakeAppTimer *t = s_fake_app_timer_head; t; t = t->next) {
    oldest = t;
  }
  return oldest && app_timer_trigger((AppTimer *)(uintptr_t)oldest->timer_id);
}

static void prv_clear_app_click_config(void) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    s_app_click_manager.recognizers[i].config = (ClickConfig){};
  }
}

// Tests
/////////////////////////////////////////////////////////////////////////

void test_watchface__initialize(void) {
  fake_app_timer_init();
  s_main_window = (Window){};
  s_menu_window = (Window){};
  s_top_window = &s_main_window;
  s_window_count = 1;
  s_app_click_manager = (ClickManager){};
  click_manager_init(&s_app_click_manager);
  s_app_task_context = (ProcessContext){.closing_state = ProcessRunState_Running};
  s_launch_count = 0;
  s_last_launch_id = INSTALL_ID_INVALID;
  s_put_count = 0;
  s_peek_dismiss_count = 0;
  s_app_click_count = 0;
  watchface_init();
}

void test_watchface__cleanup(void) {
  watchface_reset_click_manager();
  fake_app_timer_deinit();
}

void test_watchface__unclaimed_select_opens_launcher(void) {
  cl_assert(!prv_press(BUTTON_ID_SELECT));
  cl_assert(!prv_release(BUTTON_ID_SELECT));
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, APP_ID_LAUNCHER_MENU);
}

void test_watchface__unclaimed_up_runs_single_click_quick_launch(void) {
  cl_assert(!prv_press(BUTTON_ID_UP));
  cl_assert(!prv_release(BUTTON_ID_UP));
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, SINGLE_CLICK_APP_ID);
}

void test_watchface__unclaimed_down_hold_quick_launches(void) {
  cl_assert(!prv_press(BUTTON_ID_DOWN));
  cl_assert(prv_fire_oldest_timer());
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, QUICK_LAUNCH_APP_ID);
  cl_assert(!prv_release(BUTTON_ID_DOWN));
  cl_assert_equal_i(s_launch_count, 1);
}

void test_watchface__claimed_select_goes_to_the_app(void) {
  s_app_click_manager.recognizers[BUTTON_ID_SELECT].config.click.handler = prv_handler;

  cl_assert(prv_press(BUTTON_ID_SELECT));
  cl_assert(prv_release(BUTTON_ID_SELECT));
  cl_assert_equal_i(s_launch_count, 0);

  // Up and Down are still the kernel's.
  cl_assert(!prv_press(BUTTON_ID_DOWN));
  cl_assert(!prv_release(BUTTON_ID_DOWN));
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, SINGLE_CLICK_APP_ID);
}

void test_watchface__any_handler_claims_the_button(void) {
  ClickConfig *cfg = &s_app_click_manager.recognizers[BUTTON_ID_UP].config;

  cfg->long_click.handler = prv_handler;
  cl_assert(prv_press(BUTTON_ID_UP));
  cl_assert(prv_release(BUTTON_ID_UP));

  *cfg = (ClickConfig){.multi_click.handler = prv_handler};
  cl_assert(prv_press(BUTTON_ID_UP));
  cl_assert(prv_release(BUTTON_ID_UP));

  *cfg = (ClickConfig){.raw.down_handler = prv_handler};
  cl_assert(prv_press(BUTTON_ID_UP));
  cl_assert(prv_release(BUTTON_ID_UP));

  // A context alone is not a handler.
  *cfg = (ClickConfig){.context = &s_main_window};
  cl_assert(!prv_press(BUTTON_ID_UP));
  cl_assert(!prv_release(BUTTON_ID_UP));
  cl_assert_equal_i(s_launch_count, 1);
}

void test_watchface__claimed_hold_is_the_apps(void) {
  s_app_click_manager.recognizers[BUTTON_ID_DOWN].config.click.handler = prv_handler;

  cl_assert(prv_press(BUTTON_ID_DOWN));
  // The kernel started no long-press timer of its own.
  cl_assert(!prv_fire_oldest_timer());
  cl_assert(prv_release(BUTTON_ID_DOWN));
  cl_assert_equal_i(s_launch_count, 0);
}

void test_watchface__press_is_latched_at_button_down(void) {
  ClickConfig *cfg = &s_app_click_manager.recognizers[BUTTON_ID_SELECT].config;

  // Claimed at down: the up follows even if the handler went away meanwhile.
  cfg->click.handler = prv_handler;
  cl_assert(prv_press(BUTTON_ID_SELECT));
  prv_clear_app_click_config();
  cl_assert(prv_release(BUTTON_ID_SELECT));
  cl_assert_equal_i(s_launch_count, 0);

  // Not claimed at down: a handler that appears mid-press does not take it.
  cl_assert(!prv_press(BUTTON_ID_SELECT));
  cfg->click.handler = prv_handler;
  cl_assert(!prv_release(BUTTON_ID_SELECT));
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, APP_ID_LAUNCHER_MENU);
}

void test_watchface__modal_focus_releases_the_latch(void) {
  s_app_click_manager.recognizers[BUTTON_ID_UP].config.click.handler = prv_handler;
  cl_assert(prv_press(BUTTON_ID_UP));
  watchface_reset_click_manager();
  cl_assert(!prv_release(BUTTON_ID_UP));
  cl_assert_equal_i(s_launch_count, 0);
}

void test_watchface__closing_face_gets_nothing(void) {
  s_app_click_manager.recognizers[BUTTON_ID_UP].config.click.handler = prv_handler;
  cl_assert(prv_press(BUTTON_ID_UP));
  s_app_task_context.closing_state = ProcessRunState_ForceClosing;
  cl_assert(!prv_release(BUTTON_ID_UP));
  s_app_task_context.closing_state = ProcessRunState_Running;
  // The latch did not outlive the press.
  s_app_click_manager.recognizers[BUTTON_ID_UP].config.click.handler = nullptr;
  cl_assert(!prv_release(BUTTON_ID_UP));
}

void test_watchface__no_window_claims_nothing(void) {
  s_app_click_manager.recognizers[BUTTON_ID_SELECT].config.click.handler = prv_handler;
  s_top_window = nullptr;
  s_window_count = 0;
  cl_assert(!prv_press(BUTTON_ID_SELECT));
  cl_assert(!prv_release(BUTTON_ID_SELECT));
  cl_assert_equal_i(s_last_launch_id, APP_ID_LAUNCHER_MENU);
}

static void prv_set_back_handler(void) {
  s_app_click_manager.recognizers[BUTTON_ID_BACK].config.click.handler = prv_handler;
  s_main_window.overrides_back_button = true;
}

void test_watchface__back_without_handler_dismisses_the_peek(void) {
  cl_assert(!prv_press(BUTTON_ID_BACK));
  cl_assert(!prv_release(BUTTON_ID_BACK));
  cl_assert_equal_i(s_peek_dismiss_count, 1);
  cl_assert_equal_i(s_put_count, 0);
}

void test_watchface__back_is_forwarded_for_a_back_handler(void) {
  prv_set_back_handler();
  // The raw Back press is never the app's...
  cl_assert(!prv_press(BUTTON_ID_BACK));
  cl_assert(!prv_release(BUTTON_ID_BACK));
  // ...but the recognized short press is.
  cl_assert_equal_i(s_peek_dismiss_count, 0);
  cl_assert_equal_i(s_put_count, 2);
  cl_assert_equal_i(s_put_events[0].type, PEBBLE_BUTTON_DOWN_EVENT);
  cl_assert_equal_i(s_put_events[1].type, PEBBLE_BUTTON_UP_EVENT);
  cl_assert_equal_i(s_put_events[0].button.button_id, BUTTON_ID_BACK);
  cl_assert_equal_i(s_put_events[0].task_mask, 1 << PebbleTask_KernelMain);
}

void test_watchface__removed_back_handler_gives_back_the_peek(void) {
  // overrides_back_button stays set on a window once it subscribed Back; the
  // current click config decides.
  s_main_window.overrides_back_button = true;
  cl_assert(!prv_press(BUTTON_ID_BACK));
  cl_assert(!prv_release(BUTTON_ID_BACK));
  cl_assert_equal_i(s_peek_dismiss_count, 1);
  cl_assert_equal_i(s_put_count, 0);
}

void test_watchface__back_pops_a_pushed_window(void) {
  // A second window without a Back handler: Back goes to the app to pop it.
  s_top_window = &s_menu_window;
  s_window_count = 2;
  cl_assert(!prv_press(BUTTON_ID_BACK));
  cl_assert(!prv_release(BUTTON_ID_BACK));
  cl_assert_equal_i(s_put_count, 2);
  cl_assert_equal_i(s_peek_dismiss_count, 0);
}

void test_watchface__pushed_window_gets_every_button(void) {
  // E.g. a frozen ActionMenu, whose click config is empty.
  s_top_window = &s_menu_window;
  s_window_count = 2;
  const ButtonId buttons[] = {BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN};
  for (unsigned i = 0; i < ARRAY_LENGTH(buttons); i++) {
    cl_assert(prv_press(buttons[i]));
    cl_assert(prv_release(buttons[i]));
  }
  cl_assert_equal_i(s_launch_count, 0);
}

void test_watchface__back_hold_still_quick_launches(void) {
  prv_set_back_handler();
  cl_assert(!prv_press(BUTTON_ID_BACK));
  cl_assert(prv_fire_oldest_timer());
  cl_assert(!prv_release(BUTTON_ID_BACK));
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, QUICK_LAUNCH_APP_ID);
  cl_assert_equal_i(s_put_count, 0);
}

void test_watchface__back_up_combo_with_a_claimed_up(void) {
  s_app_click_manager.recognizers[BUTTON_ID_UP].config.click.handler = prv_handler;

  cl_assert(!prv_press(BUTTON_ID_BACK));
  cl_assert(prv_press(BUTTON_ID_UP));
  // The combo timer is the only timer left: the Back hold timer was reset.
  cl_assert(prv_fire_oldest_timer());
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_last_launch_id, COMBO_APP_ID);
  cl_assert(prv_release(BUTTON_ID_UP));
  cl_assert(!prv_release(BUTTON_ID_BACK));
  cl_assert_equal_i(s_launch_count, 1);
  cl_assert_equal_i(s_put_count, 0);
}

// The app gets the raw press, so its own recognizer does hold-to-repeat, as an
// ActionMenu's Up and Down do.
void test_watchface__claimed_repeating_click_repeats_in_the_app(void) {
  s_top_window = &s_menu_window;
  s_window_count = 2;
  ClickConfig *cfg = &s_app_click_manager.recognizers[BUTTON_ID_DOWN].config;
  cfg->click.handler = prv_count_app_click;
  cfg->click.repeat_interval_ms = 100;

  cl_assert(prv_press(BUTTON_ID_DOWN));
  prv_deliver_to_app(PEBBLE_BUTTON_DOWN_EVENT, BUTTON_ID_DOWN);
  cl_assert_equal_i(s_app_click_count, 1);
  // Repetition delay, then two repeats.
  cl_assert(prv_fire_oldest_timer());
  cl_assert(prv_fire_oldest_timer());
  cl_assert(prv_fire_oldest_timer());
  cl_assert_equal_i(s_app_click_count, 4);
  cl_assert(prv_release(BUTTON_ID_DOWN));
  prv_deliver_to_app(PEBBLE_BUTTON_UP_EVENT, BUTTON_ID_DOWN);
  cl_assert(!prv_fire_oldest_timer());
  cl_assert_equal_i(s_app_click_count, 4);
  cl_assert_equal_i(s_launch_count, 0);
}
