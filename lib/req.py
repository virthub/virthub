import json
import socket
import struct
from lib.log import log_err
from defaults import MONITOR_PORT

def send_pkt(sock, buf):
    if not sock:
        return
    body = json.dumps(buf)
    head = struct.pack('i', len(body))
    return sock.send(head + body)

def recv_pkt(sock):
    if not sock:
        return
    head = sock.recv(len(struct.pack('i', 0)))
    if not head:
        return
    size = struct.unpack('i', head)[0]
    if size > 0:
        body = sock.recv(size)
        if body:
            return json.loads(body)

class Client(object):
    def __init__(self, name, addr, timeout):
        self._sock = None
        self._name = name
        self._addr = addr
        self._timeout = timeout

    def __getattr__(self, op):
        self._op = op
        return self._request

    def _open(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.connect((self._addr, MONITOR_PORT))

    def _close(self):
        if self._sock:
            self._sock.close()
            self._sock = None

    def _send(self, buf):
        return send_pkt(self._sock, buf)

    def _recv(self):
        return recv_pkt(self._sock)

    def _request(self, *args):
        try:
            self._open()
            cmd = {'name':self._name, 'op':self._op, 'args':args, 'timeout':self._timeout}
            self._send(cmd)
            return self._recv()
        except:
            log_err(self, 'failed')
        finally:
            self._close()

class Request(object):
    def __init__(self, addr, timeout=None):
        self._addr = addr
        self._timeout = timeout

    def __getattr__(self, name):
        return Client(name, self._addr, self._timeout)
