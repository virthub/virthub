from lib import edge
from lib.hb import HB
from lib.log import log
from lib.util import ifaddr

PRINT = True

class Network:
    def __init__(self):
        self._hb = HB()
        self._name = None
        self._local = True

    def _log(self, text):
        if PRINT:
            log('network: ' + text)

    def create(self, name):
        self._log('create, name=%s' % name)
        self._name = name

    def start(self, name, key=None):
        self._log('start, name=%s' % name)
        if key:
            addr = edge.start(name, key)
            self._local = False
        else:
            addr = ifaddr()
        self._hb.add(name, addr)

    def stop(self, name):
        self._log('stop, name=%s' % name)
        self._hb.remove(name)
        if not self._local:
            edge.stop(name)

    def remove(self, name):
        self._log('remove, name=%s' % name)
        if name != self._name:
            self._hb.remove(name)
        if not self._local:
            edge.remove(name)

    def clean(self, name):
        pass
