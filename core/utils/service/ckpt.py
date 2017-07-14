# ckpt.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import fs
import shutil
from subprocess import Popen
from core.lib.log import log
from core.conf.defaults import CKPT
from core.conf.env import PATH_INIT, PATH_RUN, PATH_VMAP
from core.lib.util import DEVNULL, DIR_MODE, chkpid, kill, umount

PRINT = True

VMAP_NAME = 'vmap'
CKPT_NAME = 'ckpt'
CKPT_MODULE = CKPT_NAME + '.ko'

def _log(text):
	if PRINT:
		log('ckpt: %s' % text)

def _get_path_run(name):
	return os.path.join(PATH_RUN, 'ckpt.%s' % name)

def _get_path(name):
	return os.path.join(fs.get_mountpoint(name), 'var', 'ckpt')

def get_path_vmap(name):
	return os.path.join(PATH_VMAP, name)

def _start(name):
	vmap = os.path.join(PATH_INIT, VMAP_NAME)
	path = _get_path(name)
	if not os.path.isdir(path):
		os.makedirs(path, DIR_MODE)
	mountpoint = get_path_vmap(name)
	_log('start, vmap=%s' % mountpoint)
	if not os.path.isdir(mountpoint):
		os.makedirs(mountpoint, DIR_MODE)
	cmd = [vmap, path, mountpoint]
	pid = Popen(cmd, stdout=DEVNULL, stderr=DEVNULL).pid
	if not chkpid(pid):
		log('failed to start ckpt')
		raise Exception('failed to start ckpt')
	with open(_get_path_run(name), 'w') as f:
		f.write(str(pid))

def _release(name):
	path = _get_path_run(name)
	if os.path.exists(path):
		with open(path, 'r') as f:
			pid = int(f.readlines()[0].strip())
		kill(pid)
		os.remove(path)

	path = get_path_vmap(name)
	if os.path.exists(path):
		umount(path)
		shutil.rmtree(path, ignore_errors=True)

def _create(name):
	path = os.path.join(PATH_INIT, CKPT_MODULE)
	if not os.path.exists(path):
		raise Exception('Error: failed to find %s' % path)
	_log('create, path=%s' % path)
	os.system('rmmod %s 2>/dev/null' % CKPT_NAME)
	os.system('insmod %s' % path);
	_start(name)

def create(name):
	if CKPT:
		_create(name)

def start(name):
	if CKPT:
		_start(name)

def stop(name):
	if CKPT:
		_release(name)

def remove(name):
	if CKPT:
		_release(name)
