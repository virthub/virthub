import time
import redis
from lib.log import log
from defaults import MDS_PORT
from lib.util import name2addr, close_port, popen

PRINT = True
RETRY_MAX = 5
RETRY_INTERVAL = 0.1

def _log(text):
    if PRINT:
        log('mds: ' + text)

def _check_server(srv):
    for _ in range(RETRY_MAX):
        try:
            if (srv.ping()):
                return True
            else:
                time.sleep(RETRY_INTERVAL)
        except:
            time.sleep(RETRY_INTERVAL)

def create(name):
    host = name2addr(name)
    cmd = ['redis-server', '--bind', host, '--port', str(MDS_PORT)]
    popen(cmd)
    srv = redis.StrictRedis(host=host, port=MDS_PORT)
    if not _check_server(srv):
        raise Exception('failed to create')
    srv.flushall()
    _log('create, name=%s' % name)

def remove(name):
    close_port(MDS_PORT)
