from lib.log import log
from lib import container, ckpt, ipc, mds

PRINT = True

class Capsule:
    def _log(self, text):
        if PRINT:
            log('capsule: ' + text)

    def clean(self, name):
        container.clean(name)

    def create(self, name):
        self._log('create, name=%s' % name)
        self._log('creating mds ...')
        mds.create(name)

        self._log('creating ckpt ...')
        ckpt.create(name)

        self._log('creating ipc ...')
        ipc.create(name)

        self._log('creating container ...')
        container.create(name)

    def start(self, name, key=None):
        self._log('start, name=%s' % name)
        self._log('starting ckpt ...')
        ckpt.start(name)

        self._log('starting ipc ...')
        ipc.start(name)

        self._log('starting container ...')
        container.start(name)

    def stop(self, name):
        self._log('stop, name=%s' % name)
        self._log('stopping container ...')
        container.stop(name)

        self._log('stopping ipc ...')
        ipc.stop(name)

        self._log('stopping ckpt ...')
        ckpt.stop(name)

    def remove(self, name):
        self._log('remove, name=%s' % name)
        self._log('removing container ...')
        container.remove(name)

        self._log('removing ipc ...')
        ipc.remove(name)

        self._log('removing ckpt ...')
        ckpt.remove(name)

        self._log('removing mds ...')
        mds.remove(name)
