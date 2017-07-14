# start.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import sys
import argparse
import subprocess

_name = subprocess.check_output('readlink -f %s' % sys.argv[0], shell=True)
_path = os.path.dirname(_name).decode("utf-8")
_dir = os.path.dirname(_path)
sys.path.append(_dir)
sys.path.append(os.path.join(_dir, 'conf'))

import core

if __name__ == '__main__':
	backend = False
	frontend = False
	parser = argparse.ArgumentParser()
	parser.add_argument('-f', '--frontend', action='store_true')
	parser.add_argument('-b', '--backend', action='store_true')
	parser.add_argument('-a', '--all', action='store_true')
	args = parser.parse_args(sys.argv[1:])
	if args.frontend or args.all:
		frontend = True
	if args.backend or args.all:
		backend = True
	core.start(frontend, backend)
