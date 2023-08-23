import time
from lib.control import Control
from threading import Thread, Lock
from lib.util import HEARTBEAT_INTERVAL

SLEEP_TIME = 1 # sec

def excl(func):
    def _excl(*args, **kwargs):
        self = args[0]
        self._lock.acquire()
        try:
            return func(*args, **kwargs)
        finally:
            self._lock.release()
    return _excl

class Heartbeat(Control):
    def __init__(self):
        self._nodes = {}
        self._lock = Lock()
        Thread(target=self._check).start()

    @excl
    def _check_nodes(self):
        expired = []
        for i in self._nodes:
            if time.time() - self._nodes[i] >= HEARTBEAT_INTERVAL:
                expired.append(i)
                #TODO:
        for i in expired:
            del self._nodes[i]

    def _check(self):
        while True:
            time.sleep(SLEEP_TIME)
            self._check_nodes()

    @excl
    def touch(self, name):
        if self._nodes.has_key(name):
            self._nodes.update({name:time.time()})
            ret = True
        else:
            ret = False
        return ret

    @excl
    def start(self, name):
        self._nodes.update({name:time.time()})
        return True

    @excl
    def stop(self, name):
        if self._nodes.has_key(name):
            del self._nodes[name]
        return True

    def clean(self, name):
        pass
