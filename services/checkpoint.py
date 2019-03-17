# checkpoint.py
#
# Copyright (C) 2019 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

from lib.control import Control
from lib.log import log, log_get

PRINT = True

class Checkpoint(Control):
    def _log(self, text):
        if PRINT:
            log('checkpoint: ' + text)

    def stop(self, name):
        self._log('saving, name=%s' % str(name))
        return True

    def start(self, name):
        self._log('restoring, name=%s' % str(name))
        return True

    def clean(self, name):
        pass
