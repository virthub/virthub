# util.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import sys
import time
import psutil
import socket
import struct
from signal import SIGKILL
from core.conf.defaults import IFNAME
from netifaces import ifaddresses, AF_INET

DIR_MODE = 0o755
HEARTBEAT_INTERVAL = 32 # seconds
HEARTBEAT_SAFE = HEARTBEAT_INTERVAL / 2 # seconds

RETRY_MAX = 5
WAIT_TIME = 0.1 # seconds
RETRY_INTERVAL = 0.2 # seconds

DEVNULL = open(os.devnull, 'wb')

_ifaddr = None

def zmqaddr(addr, port):
	return 'tcp://%s:%d' % (str(addr), int(port))

def ifaddr(ifname=IFNAME):
	global _ifaddr
	if ifname == IFNAME and _ifaddr:
		return _ifaddr
	else:
		iface = ifaddresses(ifname)[AF_INET][0]
		addr = iface['addr']
		if ifname == IFNAME:
			_ifaddr = addr
		return addr

def name2addr(name):
	return socket.inet_ntoa(struct.pack('I', int(name, 16)))

def addr2name(addr):
	return '%08x' % struct.unpack('I', socket.inet_aton(addr))[0]

def close_port(port):
	os.system('lsof -i:%d -Fp | cut -c2- | xargs --no-run-if-empty kill -9' % port)

def umount(path):
	os.system('umount -lf %s 2>/dev/null' % path)
	time.sleep(WAIT_TIME)

def kill(pid):
	try:
		if psutil.Process(pid).status() != psutil.STATUS_ZOMBIE:
			os.kill(pid, SIGKILL)
	except:
		pass

def killall(name):
	os.system('killall %s 2>/dev/null' % name)

def chkpid(pid):
	for _ in range(RETRY_MAX):
		try:
			if psutil.Process(pid).status() == psutil.STATUS_ZOMBIE:
				return False
			else:
				return True
		except:
			time.sleep(RETRY_INTERVAL)

def chkaddr(name):
	for _ in range(RETRY_MAX):
		try:
			ret = ifaddr(name)
			if ret:
				return ret
			else:
				time.sleep(RETRY_INTERVAL)
		except:
			time.sleep(RETRY_INTERVAL)
