# monitor.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import socket
from threading import Thread
from core.lib.log import log_err
from multiprocessing import TimeoutError
from core.ctrl.heartbeat import Heartbeat
from multiprocessing.pool import ThreadPool
from core.conf.defaults import MONITOR_PORT
from core.lib.req import send_pkt, recv_pkt
from core.ctrl.checkpoint import Checkpoint

class Monitor(Thread):
	def _add_ctrl(self, ctrl):
		self._ctrl.update({str(ctrl):ctrl})

	def _init_ctrl(self):
		self._ctrl = {}
		self._add_ctrl(Heartbeat())
		self._add_ctrl(Checkpoint())

	def _init_srv(self):
		self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		self._srv.bind(('0.0.0.0', MONITOR_PORT))
		self._srv.listen(5)

	def __init__(self):
		Thread.__init__(self)
		self._init_ctrl()
		self._init_srv()

	def _proc(self, sock):
		try:
			req = recv_pkt(sock)
			if not req or type(req) != dict:
				log_err(self, 'failed to process, invalid request')
				return
			op = req.get('op')
			args = req.get('args')
			name = req.get('ctrl')
			timeout = req.get('timeout')
			if not op or not name:
				log_err(self, 'failed to process, invalid request')
				return
			ctrl = self._ctrl.get(name)
			if not ctrl:
				log_err(self, 'failed to process, no ctrl %s' % str(name))
				return
			pool = ThreadPool(processes=1)
			result = pool.apply_async(ctrl.proc, (op, args))
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
