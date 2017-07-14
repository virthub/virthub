# hb.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import re
import time
from core.lib.req import Request
from threading import Lock, Thread
from subprocess import check_output
from core.lib.log import log_get, log_err
from core.lib.util import name2addr, HEARTBEAT_SAFE

SLEEP_TIME = 1 # Seconds

def excl(func):
	def _excl(*args, **kwargs):
		self = args[0]
		self._lock.acquire()
		try:
			return func(*args, **kwargs)
		finally:
			self._lock.release()
	return _excl

class HB(Thread):
	def __init__(self):
		Thread.__init__(self)
		self._lock = Lock()
		self._nodes = {}
		self.start()

	def _is_alive(self, name):
		str_exit = 'pid:        -1'
		cmd = 'lxc-info -n %s' % name
		output = check_output(cmd)
		if not re.search(str_exit, output):
			return True
		else:
			return False

	@excl
	def _check_nodes(self):
		expired = []
		for i in self._nodes:
			t = self._nodes[i]['time']
			addr = self._nodes[i]['addr']
			if time.time() - t >=  HEARTBEAT_SAFE:
				if self._is_alive(i):
					if Request(name2addr(i)).heartbeat.touch(addr):
						self._nodes[i]['time'] = time.time()
					else:
						expired.append(i)
				else:
					expired.append(i)
		for i in expired:
			del self._nodes[i]

	def run(self):
		while True:
			time.sleep(SLEEP_TIME)
			self._check_nodes()

	@excl
	def add(self, name, addr):
		if self._nodes.has_key(name):
			log_err(self, 'cannot add again')
			raise Exception(log_get(self, 'cannot add again'))
		self._nodes.update({name:{'addr':addr, 'time':time.time()}})

	@excl
	def remove(self, name):
		if self._nodes.has_key(name):
			addr = self._nodes[name]
			del self._nodes[name]
			Request(name2addr(name)).heartbeat.stop(addr)
