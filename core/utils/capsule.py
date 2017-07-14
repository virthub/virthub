# capsule.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

from core.lib.log import log
from core.utils.service import container, ckpt, ipc, metadata

PRINT = True

class Capsule:
	def _log(self, text):
		if PRINT:
			log('Capsule: ' + text)

	def create(self, name):
		self._log('create, name=%s' % name)
		container.clean()

		self._log('creating metadata service')
		metadata.create(name)

		self._log('creating ckpt service')
		ckpt.create(name)

		self._log('creating ipc service')
		ipc.create(name)

		self._log('creating container service')
		container.create(name)

	def start(self, name, key=None):
		self._log('start, name=%s' % name)
		self._log('starting ckpt service')
		ckpt.start(name)

		self._log('starting ipc service')
		ipc.start(name)

		self._log('starting container service')
		container.start(name)

	def stop(self, name):
		self._log('stop, name=%s' % name)
		self._log('stopping container service')
		container.stop(name)

		self._log('stopping ipc service')
		ipc.stop(name)

		self._log('stopping ckpt service')
		ckpt.stop(name)

	def remove(self, name):
		self._log('remove, name=%s' % name)
		self._log('removing container service')
		container.remove(name)

		self._log('removing ipc service')
		ipc.remove(name)

		self._log('removing ckpt service')
		ckpt.remove(name)

		self._log('removing metadata service')
		metadata.remove(name)
