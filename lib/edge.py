import os
from lib.log import log
from hash_ring import HashRing
from defaults import BRIDGE_PORT, BRIDGE_SERVERS, PATH_RUN
from lib.util import chkaddr, kill_by_file, chkpid, save_pid, popen

NETSIZE = 30
NETMASK = '255.255.255.224'
EDGE_NAME = 'edge'

def _get_bridge(key):
    ring = HashRing(BRIDGE_SERVERS)
    return '%s:%d' % (ring.get_node(key), BRIDGE_PORT)

def _get_path_run(name):
    return os.path.join(PATH_RUN, '%s.%s.pid' % (EDGE_NAME, name))

def _release(name):
    path = _get_path_run(name)
    kill_by_file(EDGE_NAME, path)

def start(name, key):
    bridge = _get_bridge(key)
    path = _get_path_run(name)
    kill_by_file(EDGE_NAME, path)
    cmd = [EDGE_NAME, '-r', '-d', name, '-a', '0.0.0.0', '-s', NETMASK, '-c', name, '-k', key, '-l', bridge]
    pid = popen(cmd)
    if not chkpid(pid):
        log('failed to start edge node')
        raise Exception('failed to start edge node')
    save_pid(path, pid)
    cmd = ['dhclient', '-q', name]
    popen(cmd)
    ret = chkaddr(name)
    if not ret:
        log('failed to start edge node, invalid address')
        raise Exception('failed to start edge node, invalid address')
    return ret

def stop(name):
    _release(name)

def remove(name):
    _release(name)
