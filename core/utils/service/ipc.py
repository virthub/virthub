# ipc.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import time
import shutil
import psutil
from core.lib.log import log
from subprocess import Popen
from commands import getoutput
from core.conf.defaults import IPC
from core.conf.env import PATH_RUN, PATH_INIT, PATH_KLNK, PATH_LXC_LIB
from core.lib.util import DEVNULL, DIR_MODE, chkpid, kill, umount, name2addr

PRINT = True

KLNK_NAME = 'klnk'
RETRY_MAX = 10
RETRY_INTERVAL = 0.5 # sec

def _log(text):
	if PRINT:
		log('ipc: ' + text)

def get_mountpoint(name):
	return os.path.join(PATH_KLNK, name)

def get_binding(name):
	return os.path.join(PATH_LXC_LIB, name, 'rootfs', PATH_KLNK[1:])

def get_pid(name):
	path = _get_path_run(name)
	if os.path.exists(path):
		with open(path, 'r') as f:
			return int(f.readlines()[0].strip())

def _get_path_run(name):
	return os.path.join(PATH_RUN, 'ipc.%s' % name)

def _touch(path):
	for _ in range(RETRY_MAX):
		if os.path.exists(path):
			return True
		time.sleep(RETRY_INTERVAL)

def _check_klnk():
	processes = []
	for n in os.listdir(PATH_RUN):
		if n.startswith('ipc.'):
			name = n[len('ipc.'):]
			pid = get_pid(name)
			if pid:
				processes.append(pid)
	for _ in range(RETRY_MAX):
		result = getoutput("ps -e | grep %s | awk '{print $1}'" % KLNK_NAME)
		if result:
			active_processes = result.split('\n')
			if len(active_processes) == len(processes) + 1:
				for i in active_processes:
					pid = int(i)
					if psutil.Process(pid).status() != psutil.STATUS_ZOMBIE:
						if pid not in processes:
							return pid
		time.sleep(RETRY_INTERVAL)

def _start(name):
	_log('start, name=%s' % name)
	binding = get_binding(name)
	if not os.path.isdir(binding):
		os.makedirs(binding, DIR_MODE)
	mountpoint = get_mountpoint(name)
	if not os.path.isdir(mountpoint):
		os.makedirs(mountpoint, DIR_MODE)
	klnk = os.path.join(PATH_INIT, 'klnk')
	cmd = ['lxc-unshare', '-s', 'IPC', klnk, name2addr(name), mountpoint]
	_log(' '.join(cmd))
	pid = Popen(cmd, stdout=DEVNULL, stderr=DEVNULL).pid
	if not chkpid(pid):
		raise Exception("failed to start ipc")
	pid = _check_klnk()
	if not pid:
		raise Exception('failed to start ipc')
	with open(_get_path_run(name), 'w') as f:
		f.write(str(pid))
	path = os.path.join(mountpoint, 'test')
	if not _touch(path):
		raise Exception('failed to start ipc')
	os.system('mount --bind %s %s' % (mountpoint, binding))
	path = os.path.join(binding, 'test')
	if not _touch(path):
		raise Exception('failed to start ipc')

def _release(name):
	_log('release, name=%s' % name)
	try:
		path = get_binding(name)
		if os.path.exists(path):
			umount(path)

		path = get_mountpoint(name)
		if os.path.exists(path):
			umount(path)
			shutil.rmtree(path, ignore_errors=True)

		pid = get_pid(name)
		if pid:
			kill(pid)
			os.remove(_get_path_run(name))
	except:
		log("failed to release ipc, name=%s" % name)

def create(name):
	if IPC:
		_start(name)

def start(name):
	if IPC:
		_start(name)

def stop(name):
	if IPC:
		_release(name)

def remove(name):
	if IPC:
		_release(name)
