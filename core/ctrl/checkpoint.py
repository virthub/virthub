# checkpoint.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

from core.lib.log import log, log_get
from core.ctrl.control import Control

class Checkpoint(Control):
	def stop(self, name):
		log(log_get(self, 'saving, name=%s' % str(name)))
		return True

	def start(self, name):
		log(log_get(self, 'restoring, name=%s' % str(name)))
		return True
