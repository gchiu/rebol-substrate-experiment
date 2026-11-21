"""Named-event shutdown helper for the Windows HTTPS test harness.

Windows Python maps SIGTERM to TerminateProcess, so the native server cannot
run its graceful mg_stop/report path.  The Glon CLI accepts
--http-stop-event NAME on Windows; this module creates the event before the
server starts and signals it during teardown.
"""
import ctypes
import os
import uuid

if os.name == "nt":
    _k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _k32.CreateEventW.restype = ctypes.c_void_p
    _k32.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                  ctypes.c_int, ctypes.c_wchar_p]
    _k32.SetEvent.restype = ctypes.c_int
    _k32.SetEvent.argtypes = [ctypes.c_void_p]
    _k32.CloseHandle.restype = ctypes.c_int
    _k32.CloseHandle.argtypes = [ctypes.c_void_p]


def create_stop_event():
    if os.name != "nt":
        return None, None
    name = "Local\\GlonStop-" + uuid.uuid4().hex
    handle = _k32.CreateEventW(None, False, False, name)
    if not handle:
        raise ctypes.WinError()
    return name, handle


def set_stop_event(handle):
    if handle:
        _k32.SetEvent(handle)


def close_stop_event(handle):
    if handle:
        _k32.CloseHandle(handle)
