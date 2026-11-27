"""本机受控 RFB 测试端，不读取桌面、剪贴板或注入系统输入。"""
import socket
import struct
import sys


def receive(connection, count):
    data = bytearray()
    while len(data) < count:
        chunk = connection.recv(count - len(data))
        if not chunk:
            raise EOFError()
        data.extend(chunk)
    return bytes(data)


def serve(port):
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', port))
        listener.listen(1)
        listener.settimeout(180)
        print(f'LISTENING 127.0.0.1:{port}', flush=True)
        connection, _ = listener.accept()
        with connection:
            connection.settimeout(120)
            connection.sendall(b'RFB 003.008\n')
            assert receive(connection, 12) == b'RFB 003.008\n'
            connection.sendall(b'\x01\x01')  # 仅测试无认证握手
            assert receive(connection, 1) == b'\x01'
            connection.sendall(struct.pack('>I', 0))
            receive(connection, 1)  # ClientInit
            name = b'TuPigVNC controlled fixture'
            pixel_format = struct.pack('>BBBBHHHBBB3x', 32, 24, 0, 1, 255, 255, 255, 16, 8, 0)
            connection.sendall(struct.pack('>HH', 320, 240) + pixel_format + struct.pack('>I', len(name)) + name)
            print('SERVER_INIT_SENT', flush=True)
            sent = False
            while True:
                kind = receive(connection, 1)[0]
                if kind == 0:
                    receive(connection, 19)
                elif kind == 2:
                    header = receive(connection, 3)
                    receive(connection, struct.unpack('>H', header[1:])[0] * 4)
                elif kind == 3:
                    receive(connection, 9)
                    if not sent:
                        pixels = b''.join(bytes((40 if x < 160 else 220, 160 if y < 120 else 40, 220 if x < 160 else 40, 0))
                                          for y in range(240) for x in range(320))
                        connection.sendall(b'\0\0\0\1' + struct.pack('>HHHHi', 0, 0, 320, 240, 0) + pixels)
                        sent = True
                        print('RAW_FRAME_SENT 320x240', flush=True)
                elif kind == 4:
                    payload = receive(connection, 7)
                    print(f'KEY down={payload[0]} keysym={struct.unpack(">I", payload[3:])[0]}', flush=True)
                elif kind == 5:
                    payload = receive(connection, 5)
                    buttons, x, y = struct.unpack('>BHH', payload)
                    print(f'POINTER buttons={buttons} x={x} y={y}', flush=True)
                elif kind == 6:
                    header = receive(connection, 7)
                    length = struct.unpack('>I', header[3:])[0]
                    if length > 1048576:
                        raise ValueError('fixture clipboard size exceeded')
                    receive(connection, length)
                    print('CLIPBOARD_MESSAGE', flush=True)
                else:
                    raise ValueError(f'unsupported message {kind}')


if __name__ == '__main__':
    try:
        serve(int(sys.argv[1]) if len(sys.argv) > 1 else 5998)
    except EOFError:
        print('CLIENT_DISCONNECTED', flush=True)
    except Exception as error:
        print(f'FAILED {type(error).__name__}: {error}', flush=True)
        raise
