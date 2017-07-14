# ftp.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
from core.lib.log import log
from pyftpdlib.servers import FTPServer
from pyftpdlib.handlers import FTPHandler
from pyftpdlib.authorizers import DummyAuthorizer

PRINT = True

def _log(text):
	if PRINT:
		log('ftp: %s' % text)

def create(path, addr, port):
	_log('create, path=%s, addr=%s, port=%d' % (path, addr, port))
	auth = DummyAuthorizer()
	auth.add_user("virthub", "virthub", path, perm="elradfmwM")
	handler = FTPHandler
	handler.authorizer = auth
	srv = FTPServer((addr, port), handler)
	srv.serve_forever()

def mount(path, addr, port):
	_log('mount, path=%s, addr=%s, port=%d' % (path, addr, port))
	os.system('curlftpfs ftp://virthub:virthub@%s:%d %s' % (addr, port, path))
