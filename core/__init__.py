import os
from env import *
from time import sleep
from defaults import *
from core.lib import ftp
from core.lib.log import log
from threading import Thread
from core.init import backend
from core.conf.defaults import *
from core.init.monitor import Monitor
from core.lib.util import close_port, ifaddr

def _clean():
	for i in [MONITOR_PORT, FS_PORT, FRONTEND_PORT, BACKEND_PORT, NOTIFIER_PORT, METADATA_PORT]:
		close_port(i)

def _start_file_server():
	if not os.path.isdir(PATH_SHARE):
		os.makedirs(PATH_SHARE, 0o755)
	ftp.create(PATH_SHARE, ifaddr(), FRONTEND_PORT)

def start(enable_frontend, enable_backend):
	_clean()
	if enable_frontend:
		if FRONTEND:
			raise Exception('Error: the current setting is no frontend mode')
		th = Thread(target=_start_file_server)
		th.start()
		if not enable_backend:
			th.join()
		else:
			sleep(1)
	if enable_backend:
		Monitor().start()
		backend.start()
