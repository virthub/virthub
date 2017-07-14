# login.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import sys
import platform
import subprocess

_ver = 3
if sys.version_info[0] != _ver:
	raise Exception('Error: python%d is required!' % _ver)

_readlink = ''
_system = platform.system()
if _system == 'Linux':
	_readlink = 'readlink'
elif _system == 'Darwin':
	_readlink = 'greadlink'
else:
	raise Exception('Error: %s is not supported.' % system)
_name = subprocess.check_output('%s -f %s' % (_readlink, sys.argv[0]), shell=True)
_path = os.path.dirname(_name).decode("utf-8")
_dir = os.path.dirname(_path)
sys.path.append(_dir)
sys.path.append(os.path.join(_dir, 'conf'))

import ui

if __name__ == '__main__':
	ui.start(_dir)
