# fs.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import subprocess
import multiprocessing
from defaults import *
from core.lib import ftp
from core.lib.log import log
from core.conf.defaults import FS_PORT, MIFS, FRONTEND
from core.conf.env import PATH_LXC_LIB, PATH_MNT, PATH_INIT, PATH_RUN, PATH_BIN, PATH_MIFS
from core.lib.util import DEVNULL, DIR_MODE, name2addr, close_port, ifaddr, umount, chkpid

PRINT = True

def _log(text):
	if PRINT:
		log('fs: %s' % text)

def get_mountpoint(name):
	return os.path.join(PATH_MNT, name)

def get_binding(name):
	return os.path.join(PATH_LXC_LIB, name, 'rootfs', 'vhub')

def _release(name):
	umount(get_binding(name))
	umount(get_mountpoint(name))

def _get_mifs_path(name):
	return os.path.join(get_mountpoint(name), 'var', 'mifs')

def _start_mifs(name):
	path = _get_mifs_path(name)
	_log('start mifs, name=%s, mifs=%s, home=%s' % (name, PATH_MIFS, path))
	if not os.path.isdir(PATH_MIFS):
		os.makedirs(PATH_MIFS, DIR_MODE)
	if not os.path.isdir(path):
		os.makedirs(path, DIR_MODE)
	cmd = [os.path.join(PATH_INIT, 'mifs'), path]
	pid = subprocess.Popen(cmd, stdout=DEVNULL, stderr=DEVNULL).pid
	if not chkpid(pid):
		log('failed to start mifs')
		raise Exception('failed to start mifs')
	path = os.path.join(PATH_RUN, 'mifs')
	with open(path, 'w') as f:
		f.write(str(pid))

def create(name):
 	dirname = get_mountpoint(name)
	if not os.path.isdir(dirname):
		os.makedirs(dirname, DIR_MODE)
	_log('create, mountpoint=%s' % dirname)
	if FRONTEND:
		ftp.mount(dirname, FRONTEND_ADDR, FRONTEND_PORT)
	path = os.path.join(dirname, 'bin')
	if not os.path.isdir(path):
		os.makedirs(path, DIR_MODE)
	os.system('mount -o bind %s %s' % (PATH_BIN, path))
	multiprocessing.Process(target=ftp.create, args=(dirname, name2addr(name), FS_PORT)).start()
	if MIFS:
		_start_mifs(name)

def start(name):
	path = get_mountpoint(name)
	if not os.path.isdir(path):
		os.makedirs(path, DIR_MODE)
	_log('start, mountpoint=%s' % path)
	ftp.mount(path, name2addr(name), FS_PORT)
	path = os.path.join(dirname, 'bin')
	if not os.path.exists(path):
		log('failed to start, name=%s' % name)
		raise Exception('Error: failed to start, name=%s' % name)

def stop(name):
	_release(name)

def remove(name):
	_release(name)
