from lib import fs
from lib.log import log

PRINT = True

class Storage:
    def _log(self, text):
        if PRINT:
            log('storage: ' + text)

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

    def clean(self, name):
        pass
