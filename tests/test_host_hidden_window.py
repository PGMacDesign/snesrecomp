"""SNESRECOMP_HIDDEN_WINDOW: the desktop host's window for automated runs
that need a real video driver but must stay off the desktop.

Source-order checks on runner/src/desktop/host_main.c: the knob is read and
the background-app and no-activate hints are set before SDL_Init (macOS sets
the app's activation policy there); SDL_WINDOW_HIDDEN is in the flags the
window is created with (a window shown and then hidden still reaches the
window server); the in-game launcher's return never shows or raises it.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST = ROOT / "runner" / "src" / "desktop" / "host_main.c"


def _source():
    return HOST.read_text(encoding="utf-8")


def test_knob_and_hints_come_before_sdl_init():
    s = _source()
    knob = s.index('HostGetenv("HIDDEN_WINDOW")')
    init = s.index("snesrecomp_sdl_init(SDL_INIT_VIDEO")
    hints = [s.index("SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, \"1\")"),
             s.index("SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, \"0\")"),
             s.index("SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_RAISED, \"0\")")]
    assert knob < min(hints) and max(hints) < init
    # "0" means visible, so a harness can pass the variable through unset.
    assert 'g_hidden_window = hidden && strcmp(hidden, "0") != 0;' in s


def test_window_is_created_hidden():
    s = _source()
    flag = s.index("if (g_hidden_window) g_win_flags |= SDL_WINDOW_HIDDEN;")
    create = s.index("snesrecomp_sdl_create_window(\n      g_window_title")
    assert flag < create


def test_nothing_shows_or_raises_a_hidden_window():
    s = _source()
    for call in ("SDL_ShowWindow(g_window)", "SDL_RaiseWindow(g_window)"):
        for m in re.finditer(re.escape(call), s):
            guard = s.rfind("if (!g_hidden_window) {", 0, m.start())
            assert guard != -1 and m.start() - guard < 120, f"{call} at {m.start()} is unguarded"
