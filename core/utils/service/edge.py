# edge.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
from core.lib.log import log
from hash_ring import HashRing
from subprocess import Popen, call
from core.conf.env import PATH_RUN
from core.lib.util import DEVNULL, chkaddr, kill, chkpid
from core.conf.defaults import BRIDGE_PORT, BRIDGE_SERVERS

NETSIZE = 30
NETMASK = '255.255.255.224'

def _get_bridge(key):
	ring = HashRing(BRIDGE_SERVERS)
	return '%s:%d' % (ring.get_node(key), BRIDGE_PORT)

def _get_path_run(name):
	return os.path.join(PATH_RUN, 'edge.%s' % name)

def _release(name):
	path = _get_path_run(name)
	if os.path.exists(path):
		with open(path, 'r') as f:
			pid = int(f.readlines()[0].strip())
		kill(pid)
		os.remove(path)

def start(name, key):
	bridge = _get_bridge(key)
	cmd = ['edge', '-r', '-d', name, '-a', '0.0.0.0', '-s', NETMASK, '-c', name, '-k', key, '-l', bridge]
	pid = Popen(cmd, stdout=DEVNULL, stderr=DEVNULL).pid
	if not chkpid(pid):
		log('failed to start edge node')
		raise Exception('failed to start edge node')
	with open(_get_path_run(name), 'w') as f:
		f.write(str(pid))
	call(['dhclient', '-q', name], stderr=DEVNULL, stdout=DEVNULL)
	ret = chkaddr(name)
	if not ret:
		log('failed to start edge node, invalid address')
		raise Exception('failed to start edge node, invalid address')
	return ret

def stop(name):
	_release(name)

def remove(name):
	_release(name)
