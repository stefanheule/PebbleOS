# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import pytest
from harness.fixtures import dut_scope_within
from harness.helpers.phone import forget_bonds, make_phone
from harness.helpers.ui import Button, Ui
from harness.helpers.voice import (
    RESULT_FAIL_SERVICE_UNAVAILABLE,
    RESULT_SUCCESS,
    SESSION_DICTATION,
    SESSION_NLP,
    VoicePhone,
    assert_session_ends,
    start,
)

pytestmark = [
    pytest.mark.requires_config(
        "CONFIG_SHELL", "CONFIG_SERVICE_VOICE", "CONFIG_SERVICE_VOICE_ENDPOINT"
    ),
]

SAMPLE_RATE_HZ = 16000
FRAME_SAMPLES = 320
RECORD_FRAMES = 50
VOICE_DEMO_UUID = "5c0e6a41-2b8d-4f2e-9a37-1dc4806b52f3"


@pytest.fixture(scope=dut_scope_within("module"))
def voice(dut, lab_setup, results_dir):
    """The phone, answering dictation; one for the module, as the watch
    keeps a single bond."""
    reason = lab_setup.lacks("phone")
    if reason:
        pytest.skip(reason)
    forget_bonds(results_dir)
    phone = make_phone(dut, lab_setup.phone, results_dir).connect()
    try:
        yield VoicePhone(phone.pebble)
    finally:
        phone.disconnect()


@pytest.fixture(autouse=True)
def answering(voice):
    voice.result = RESULT_SUCCESS


@pytest.mark.parametrize(
    "kind,session_type", [("dictation", SESSION_DICTATION), ("nlp", SESSION_NLP)]
)
def test_session(prompt, voice, kind, session_type):
    since = len(voice.sessions)
    start(prompt, kind)
    session = voice.wait_session(since=since)
    assert session.session_type == session_type
    assert not session.app_initiated
    assert session.speex.sample_rate == SAMPLE_RATE_HZ
    assert session.speex.frame_size == FRAME_SAMPLES

    voice.wait_frames(session, RECORD_FRAMES)
    prompt("voice stop")
    voice.wait_stopped(session)
    assert all(frame for _, frame in session.frames)

    if session_type == SESSION_DICTATION:
        voice.send_result(session, ["hello", "world"])
    else:
        voice.send_nlp_result(session, "call mom")
    assert_session_ends(prompt, voice)


def test_rejected(dut, prompt, voice):
    voice.result = RESULT_FAIL_SERVICE_UNAVAILABLE
    since = len(voice.sessions)
    mark = dut.logs.mark()
    start(prompt)
    session = voice.wait_session(since=since)
    dut.wait_for_log(r"Error occurred setting up session: 1", since=mark)
    assert not session.frames
    assert_session_ends(prompt, voice)


def test_cancelled(prompt, voice):
    since = len(voice.sessions)
    start(prompt)
    session = voice.wait_session(since=since)
    voice.wait_frames(session, 1)
    prompt("voice cancel")
    assert_session_ends(prompt, voice)


@pytest.fixture
def recording_demo(dut, voice):
    ui = Ui(dut)
    ui.go_home()
    ui.launch_app(VOICE_DEMO_UUID)
    ui.wait_idle()
    assert ui.top_window() == "Voice Demo"
    ui.press(Button.UP)  # Return results directly, without a confirmation screen.
    since = len(voice.sessions)
    mark = dut.logs.mark()
    ui.press(Button.SELECT)
    session = voice.wait_session(since=since)
    try:
        assert session.session_type == SESSION_DICTATION
        voice.wait_frames(session, 25)
        assert ui.top_window() == "Voice Window"
        yield ui, session, mark
    finally:
        ui.go_home()


@pytest.mark.requires_config("CONFIG_DEMO_APP_VOICE_DEMO")
@pytest.mark.parametrize("button", [Button.DOWN, Button.SELECT])
def test_recording_stop_button(dut, voice, recording_demo, button):
    ui, session, mark = recording_demo
    ui.press(button)
    # Require a button stop before the recording's 15-second timeout.
    voice.wait_stopped(session, timeout=2)
    voice.send_result(session, ["hello", "world"])
    dut.wait_for_log(r"Dictation status 0 after", timeout=5, since=mark)
    assert ui.top_window() == "Voice Demo"


@pytest.mark.requires_config("CONFIG_DEMO_APP_VOICE_DEMO")
def test_recording_back_cancels(dut, voice, recording_demo):
    ui, session, mark = recording_demo
    ui.press(Button.BACK)
    voice.wait_stopped(session, timeout=2)
    dut.wait_for_log(r"Dictation status 1 after", timeout=5, since=mark)
    assert ui.top_window() == "Voice Demo"
