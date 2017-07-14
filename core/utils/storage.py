# storage.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

from core.lib.log import log
from core.utils.service import fs

PRINT = True

class Storage:
	def _log(self, text):
		if PRINT:
			log('Storage: ' + text)

	def create(self, name):
		self._log('create, name=%s' % name)
		fs.create(name)

	def start(self, name, key=None):
		self._log('start, name=%s' % name)
		fs.start(name)

	def stop(self, name):
		self._log('stop, name=%s' % name)
		fs.stop(name)

	def remove(self, name):
		self._log('remove, name=%s' % name)
		fs.remove(name)
