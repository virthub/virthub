# virthub.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import shutil
from core.lib.log import log
from core.utils.capsule import Capsule
from core.utils.storage import Storage
from core.utils.network import Network
from core.lib.util import DIR_MODE, umount
from core.conf.env import PATH_RUN, PATH_MNT, PATH_BIN

PRINT = True

_utils = [Network(), Storage(), Capsule()]

def _log(text):
	if PRINT:
		log('virthub: ' + text)

def _check_path():
	if os.path.isdir(PATH_RUN):
		shutil.rmtree(PATH_RUN)
	os.makedirs(PATH_RUN, DIR_MODE)
	if os.path.isdir(PATH_MNT):
		for name in os.listdir(PATH_MNT):
			path = os.path.join(PATH_MNT, name)
			try:
				for i in os.listdir(path):
					n = os.path.join(path, i)
					if not os.path.islink(n):
						umount(n)
				umount(path)
				shutil.rmtree(path)
			except:
				pass
	else:
		os.makedirs(PATH_MNT, DIR_MODE)
	if not os.path.isdir(PATH_BIN):
		os.makedirs(PATH_BIN, DIR_MODE)

def create(name):
	_log('create, name=%s' % name)
	_check_path()
	for item in _utils:
		item.create(name)
	_log('finish creating %s' % name)

def start(name, key):
	_log('start, name=%s' % name)
	for item in _utils:
		item.start(name, key)
	_log('finish starting %s' % name)

def stop(name):
	_log('stop, name=%s' % name)
	for item in _utils:
		item.stop(name)
	_log('finish stopping %s' % name)

def remove(name):
	_log('remove, name=%s' % name)
	for item in _utils:
		item.remove(name)
	_log('finish removing %s' % name)
