# virtdev.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import md5
import shelve
from core.lib.util import DIR_MODE
from env import PATH_VIRTDEV, PATH_VIRTDEV_VAR

def create(user, password):
	if not os.path.isdir(PATH_VIRTDEV_VAR):
		os.makedirs(PATH_VIRTDEV_VAR, DIR_MODE)
	path = os.path.join(PATH_VIRTDEV_VAR, 'user')
	d = shelve.open(path)
	try:
		d['user'] = user
		d['password'] = md5.new(password).hexdigest()
	finally:
		d.close()
	os.system('%s 1>/dev/null&' % os.path.join(PATH_VIRTDEV, 'bin', 'virtdev'))
