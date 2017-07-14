# network.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

from core.lib.log import log
from core.utils.service import edge
from core.utils.service.hb import HB

PRINT = True

class Network:
	def __init__(self):
		self._name = None

	def _log(self, text):
		if PRINT:
			log('Network: ' + text)

	def create(self, name):
		self._log('create, name=%s' % name)
		self._hb = HB()
		self._name = name

	def start(self, name, key):
		self._log('start, name=%s' % name)
		addr = edge.start(name, key)
		self._hb.add(name, addr)

	def stop(self, name):
		self._log('stop, name=%s' % name)
		self._hb.remove(name)
		edge.stop(name)

	def remove(self, name):
		self._log('remove, name=%s' % name)
		if name != self._name:
			self._hb.remove(name)
		edge.remove(name)
