# monitor.py
#
# Copyright (C) 2019 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import socket
from lib.log import log_err
from threading import Thread
from defaults import MONITOR_PORT
from lib.req import send_pkt, recv_pkt
from multiprocessing import TimeoutError
from services.heartbeat import Heartbeat
from services.checkpoint import Checkpoint
from multiprocessing.pool import ThreadPool

class Monitor(Thread):
    def _add(self, service):
        self._services.update({str(service):service})

    def _init(self):
        self._services = {}
        self._add(Heartbeat())
        self._add(Checkpoint())

    def _listen(self):
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.bind(('0.0.0.0', MONITOR_PORT))
        self._srv.listen(5)

    def __init__(self):
        Thread.__init__(self)
        self._init()
        self._listen()

    def _proc(self, sock):
        try:
            req = recv_pkt(sock)
            if not req or type(req) != dict:
                log_err(self, 'failed to process, invalid request')
                return
            op = req.get('op')
            args = req.get('args')
            name = req.get('name')
            timeout = req.get('timeout')
            if not op or not name:
                log_err(self, 'failed to process, invalid request')
                return
            service = self._services.get(name)
            if not service:
                log_err(self, 'failed to process, cannot find service %s' % str(name))
                return
            pool = ThreadPool(processes=1)
            result = pool.apply_async(service.proc, (op, args))
            pool.close()
            try:
                send_pkt(sock, result.get(timeout))
            except TimeoutError:
                log_err(self, 'timeout')
            finally:
                pool.join()
        finally:
            sock.close()

    def run(self):
        while True:
            try:
                sock = self._srv.accept()[0]
                Thread(target=self._proc, args=(sock,)).start()
            except:
                log_err(self, 'failed')
