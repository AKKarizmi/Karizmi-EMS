import argparse
import ctypes as ct
from ctypes import wintypes as wt
import json
from pathlib import Path
import subprocess
import time
import uuid
from PIL import Image

user = ct.WinDLL('user32', use_last_error=True)
gdi = ct.WinDLL('gdi32', use_last_error=True)
user.SetProcessDpiAwarenessContext(ct.c_void_p(-4))
callback_type = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user.EnumWindows.argtypes = [callback_type, wt.LPARAM]
user.EnumChildWindows.argtypes = [wt.HWND, callback_type, wt.LPARAM]
user.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user.SendMessageW.restype = ct.c_ssize_t
user.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ct.c_int]
user.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ct.c_int]
user.GetWindowRect.argtypes = [wt.HWND, ct.POINTER(wt.RECT)]
user.GetClientRect.argtypes = [wt.HWND, ct.POINTER(wt.RECT)]
user.GetWindowThreadProcessId.argtypes = [wt.HWND, ct.POINTER(wt.DWORD)]
user.IsWindowVisible.argtypes = [wt.HWND]
user.GetDlgCtrlID.argtypes = [wt.HWND]
user.GetDpiForWindow.argtypes = [wt.HWND]
user.GetDlgItem.argtypes = [wt.HWND, ct.c_int]
user.GetDlgItem.restype = wt.HWND
user.SetWindowTextW.argtypes = [wt.HWND, wt.LPCWSTR]
user.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
user.GetDC.argtypes = [wt.HWND]
user.GetDC.restype = wt.HDC
user.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
user.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
gdi.CreateCompatibleDC.argtypes = [wt.HDC]
gdi.CreateCompatibleDC.restype = wt.HDC
gdi.CreateCompatibleBitmap.argtypes = [wt.HDC, ct.c_int, ct.c_int]
gdi.CreateCompatibleBitmap.restype = wt.HBITMAP
gdi.SelectObject.argtypes = [wt.HDC, wt.HANDLE]
gdi.SelectObject.restype = wt.HANDLE
gdi.GetObjectW.argtypes = [wt.HANDLE, ct.c_int, ct.c_void_p]
gdi.DeleteObject.argtypes = [wt.HANDLE]
gdi.DeleteDC.argtypes = [wt.HDC]
gdi.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ct.c_void_p, ct.c_void_p, wt.UINT]

class LogFont(ct.Structure):
    _fields_ = [(name, wt.LONG) for name in ('height', 'width', 'escapement', 'orientation', 'weight')] + [
        (name, wt.BYTE) for name in ('italic', 'underline', 'strikeout', 'charset', 'out', 'clip', 'quality', 'pitch')
    ] + [('face', wt.WCHAR * 32)]

class BitmapInfo(ct.Structure):
    _fields_ = [('size', wt.DWORD), ('width', wt.LONG), ('height', wt.LONG), ('planes', wt.WORD),
                ('bits', wt.WORD), ('compression', wt.DWORD), ('image_size', wt.DWORD),
                ('xppm', wt.LONG), ('yppm', wt.LONG), ('used', wt.DWORD), ('important', wt.DWORD)]

def window_text(handle, class_name=False):
    buffer = ct.create_unicode_buffer(2048)
    (user.GetClassNameW if class_name else user.GetWindowTextW)(handle, buffer, len(buffer))
    return buffer.value

def windows(process_id, include_hidden=False):
    found = []
    @callback_type
    def collect(handle, unused):
        owner = wt.DWORD()
        user.GetWindowThreadProcessId(handle, ct.byref(owner))
        if owner.value == process_id and (include_hidden or user.IsWindowVisible(handle)):
            found.append(handle)
        return True
    user.EnumWindows(collect, 0)
    return found

def inspect(handle):
    rectangle = wt.RECT()
    user.GetWindowRect(handle, ct.byref(rectangle))
    font = LogFont()
    font_handle = user.SendMessageW(handle, 0x31, 0, 0)
    if font_handle:
        gdi.GetObjectW(font_handle, ct.sizeof(font), ct.byref(font))
    return dict(id=user.GetDlgCtrlID(handle), title=window_text(handle), kind=window_text(handle, True),
                visible=bool(user.IsWindowVisible(handle)), dpi=user.GetDpiForWindow(handle),
                rect=[rectangle.left, rectangle.top, rectangle.right, rectangle.bottom],
                font_height=font.height, font_face=font.face)

def capture(handle, path):
    rectangle = wt.RECT()
    user.GetWindowRect(handle, ct.byref(rectangle))
    width, height = rectangle.right - rectangle.left, rectangle.bottom - rectangle.top
    screen = user.GetDC(handle)
    memory = gdi.CreateCompatibleDC(screen)
    bitmap = gdi.CreateCompatibleBitmap(screen, width, height)
    previous = gdi.SelectObject(memory, bitmap)
    if not user.PrintWindow(handle, memory, 2):
        raise RuntimeError('PrintWindow failed')
    gdi.SelectObject(memory, previous)
    info = BitmapInfo(ct.sizeof(BitmapInfo), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = ct.create_string_buffer(width * height * 4)
    if not gdi.GetDIBits(memory, bitmap, 0, height, pixels, ct.byref(info), 0):
        raise RuntimeError('GetDIBits failed')
    Image.frombytes('RGB', (width, height), pixels.raw, 'raw', 'BGRX').save(path)
    gdi.DeleteObject(bitmap)
    gdi.DeleteDC(memory)
    user.ReleaseDC(handle, screen)

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--test-service-stop', action='store_true')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
command = [str(args.executable.resolve())]
service_event = None
if args.test_service_stop:
    kernel = ct.WinDLL('kernel32',use_last_error=True)
    kernel.CreateEventW.argtypes = [ct.c_void_p,wt.BOOL,wt.BOOL,wt.LPCWSTR]
    kernel.CreateEventW.restype = wt.HANDLE
    kernel.SetEvent.argtypes = [wt.HANDLE]
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    event_name = 'Local\\KarizmiStudentStopTest-' + uuid.uuid4().hex
    service_event = kernel.CreateEventW(None,True,False,event_name)
    if not service_event:
        raise ct.WinError(ct.get_last_error())
    command += ['--service-agent',event_name]
process = subprocess.Popen(command, cwd=args.executable.resolve().parent)
try:
    main = None
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        for handle in windows(process.pid, True):
            kind = window_text(handle, True)
            if kind == '#32770' and 'monitoring notice' in window_text(handle).lower():
                user.PostMessageW(handle, 0x111, 1, user.GetDlgItem(handle,1))
            if kind in ('KarizmiMain','KarizmiClassroom'):
                main = handle
        if main:
            break
        time.sleep(0.1)
    if not main:
        raise RuntimeError('No Karizmi main window: ' + repr([inspect(handle) for handle in windows(process.pid)]))
    time.sleep(0.5)
    classroom = window_text(main,True) == 'KarizmiClassroom'
    if not classroom:
        if user.IsWindowVisible(main):
            raise RuntimeError('Student must start in tray after its notice')
        if any(window_text(handle,True) == 'KarizmiIndicator' for handle in windows(process.pid,True)):
            raise RuntimeError('Student floating indicator still exists')
        user.PostMessageW(main,0x8007,1,0x203)
        time.sleep(0.2)
    controls = []
    @callback_type
    def collect_child(handle, unused):
        controls.append(inspect(handle))
        return True
    user.EnumChildWindows(main, collect_child, 0)
    report = dict(main=inspect(main), controls=controls)
    (args.output / 'controls.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    capture(main, args.output / 'window.png')
    user.GetPropW.argtypes = [wt.HWND,wt.LPCWSTR]
    user.GetPropW.restype = wt.HANDLE
    if not user.GetPropW(main,'KarizmiTrayRegistered'):
        raise RuntimeError('Shell rejected tray registration')
    user.PostMessageW(main,0x10,0,0)
    time.sleep(0.2)
    if user.IsWindowVisible(main):
        raise RuntimeError('Closing main window did not hide it')
    user.PostMessageW(main,0x8007,1,0x203) # Tray callback: left double-click.
    time.sleep(0.2)
    if not user.IsWindowVisible(main):
        raise RuntimeError('Tray Open did not restore the main window')
    restart = user.RegisterWindowMessageW('TaskbarCreated')
    user.PostMessageW(main,restart,0,0)
    time.sleep(0.3)
    if not user.GetPropW(main,'KarizmiTrayRegistered'):
        raise RuntimeError('Tray registration did not recover after shell notification')
    if not classroom:
        user.PostMessageW(main, 0x111, 114, 0)
        time.sleep(0.2)
        dialogs = [handle for handle in windows(process.pid) if window_text(handle,True) == '#32770']
        if len(dialogs) != 1 or 'Advanced' not in window_text(dialogs[0]):
            raise RuntimeError('Advanced button did not open its real dialog')
        user.PostMessageW(dialogs[0],0x10,0,0)
        time.sleep(0.1)
        user.PostMessageW(main,0x111,103,0)
    else:
        user.PostMessageW(main,0x111,204,0)
        user.PostMessageW(main,0x111,203,0)
        time.sleep(0.2)
        if 'Select a discovered Student' not in window_text(user.GetDlgItem(main,207)):
            raise RuntimeError('Pair validation did not run')
        user.PostMessageW(main,0x111,205,0)
    print(json.dumps(dict(title=report['main']['title'],dpi=report['main']['dpi'],controls=len(controls),tray='close/open and shell recreation exercised',screenshot=str(args.output / 'window.png'))))
    if service_event:
        kernel.SetEvent(service_event)
    else:
        user.PostMessageW(main, 0x111, 208 if classroom else 108, 0)
    process.wait(timeout=15)
    if process.returncode!=0:
        raise RuntimeError(f'Application exited abnormally: {process.returncode:#x}')
    if service_event:
        print('Service stop signal: graceful process exit passed')
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
    if service_event:
        kernel.CloseHandle(service_event)
