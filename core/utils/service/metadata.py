# metadata.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import time
import redis
from subprocess import Popen
from core.lib.log import log
from core.conf.defaults import METADATA_PORT
from core.lib.util import DEVNULL, name2addr, close_port

PRINT = True

RETRY_MAX = 5
RETRY_INTERVAL = 0.1

def _log(text):
	if PRINT:
		log('metadata: ' + text)

def _check_server(srv):
	for _ in range(RETRY_MAX):
		try:
			if (srv.ping()):
				return True
			else:
				time.sleep(RETRY_INTERVAL)
		except:
			time.sleep(RETRY_INTERVAL)

def create(name):
	host = name2addr(name)
	cmd = ['redis-server', '--bind', host, '--port', str(METADATA_PORT)]
	Popen(cmd, stdout=DEVNULL, stderr=DEVNULL)
	srv = redis.StrictRedis(host=host, port=METADATA_PORT)
	if not _check_server(srv):
		raise Exception('failed to create')
	srv.flushall()
	_log('create, name=%s, host=%s' % (name, host))

def remove(name):
	close_port(METADATA_PORT)
