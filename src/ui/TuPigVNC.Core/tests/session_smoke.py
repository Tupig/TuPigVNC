"""加载实际 DLL，对受控 TCP 服务验证初始化、像素与输入。"""
import ctypes as c
from pathlib import Path
import subprocess
import sys
import threading


def main():
    fixture = subprocess.Popen([sys.executable, '-I', '-u', str(Path(__file__).with_name('rfb_fixture.py')), '5998'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    assert fixture.stdout.readline().startswith('LISTENING')
    dll = c.CDLL(sys.argv[1])
    initialized, updated = threading.Event(), threading.Event()
    failures = []
    init_type = c.CFUNCTYPE(None, c.c_void_p, c.c_int, c.c_int, c.c_char_p, *([c.c_int] * 5))
    frame_type = c.CFUNCTYPE(None, c.c_void_p, *([c.c_int] * 4))
    error_type = c.CFUNCTYPE(None, c.c_void_p, c.c_char_p, c.c_int)
    init = init_type(lambda *_: initialized.set())
    frame = frame_type(lambda *_: updated.set())
    error = error_type(lambda _, message, fatal: failures.append((message, fatal)))
    class Callbacks(c.Structure):
        _fields_ = [('slots', c.c_void_p * 14)]
    callbacks = Callbacks()
    for slot, callback in [(1, init), (2, frame), (3, error)]:
        callbacks.slots[slot] = c.cast(callback, c.c_void_p).value
    dll.vnc_create.argtypes = [c.POINTER(Callbacks)]
    dll.vnc_create.restype = c.c_void_p
    dll.vnc_connect.argtypes = [c.c_void_p, c.c_char_p, c.c_int, c.c_void_p, c.c_size_t]
    for name in ['vnc_start_loop', 'vnc_request_stop', 'vnc_destroy']:
        getattr(dll, name).argtypes = [c.c_void_p]
    dll.vnc_copy_frame.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_int, c.POINTER(c.c_int), c.POINTER(c.c_int)]
    dll.vnc_send_pointer.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_uint16]
    dll.vnc_send_key.argtypes = [c.c_void_p, c.c_int, c.c_uint32, c.c_uint32]
    dll.vnc_send_key_release.argtypes = [c.c_void_p, c.c_int]
    handle = dll.vnc_create(c.byref(callbacks))
    assert handle
    try:
        err = c.create_string_buffer(1024)
        assert dll.vnc_connect(handle, b'127.0.0.1', 5998, err, len(err)) == 0, err.value
        dll.vnc_start_loop(handle)
        assert initialized.wait(10), failures
        assert updated.wait(10), failures
        w, h = c.c_int(), c.c_int()
        small = (c.c_ubyte * 4)()
        assert dll.vnc_copy_frame(handle, small, 4, 4, c.byref(w), c.byref(h)) == -1
        assert (w.value, h.value) == (320, 240)
        pixels = (c.c_ubyte * (320 * 240 * 4))()
        assert dll.vnc_copy_frame(handle, pixels, len(pixels), 1280, c.byref(w), c.byref(h)) == 0
        assert bytes(pixels[:4]) == bytes([40, 160, 220, 0])
        print('PASS handshake, ServerInit, raw BGRX frame, undersized buffer rejection', flush=True)
        dll.vnc_send_pointer(handle, 12, 34, 1)
        dll.vnc_send_pointer(handle, 12, 34, 0)
        dll.vnc_send_key(handle, 65, 30, 97)
        dll.vnc_send_key_release(handle, 65)
        # 等待服务器实际观察按键释放，不以入队成功代替协议抵达。
        observed = []
        while True:
            line = fixture.stdout.readline().strip()
            assert line, 'fixture exited before input'
            observed.append(line)
            if line == 'KEY down=0 keysym=97':
                break
        assert 'POINTER buttons=1 x=12 y=34' in observed, observed
        assert 'POINTER buttons=0 x=12 y=34' in observed, observed
        print('PASS pointer press/release and key press/release reached server', flush=True)
    finally:
        dll.vnc_request_stop(handle)
        dll.vnc_destroy(handle)
        try:
            output, _ = fixture.communicate(timeout=10)
            assert 'CLIENT_DISCONNECTED' in output, output
            print('PASS stop/destroy disconnected TCP', flush=True)
        finally:
            if fixture.poll() is None:
                fixture.kill()
                fixture.wait()


if __name__ == '__main__':
    main()
