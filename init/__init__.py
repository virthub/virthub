from defaults import *
from init import virthub
from init.monitor import Monitor
from lib.util import close_port, addr2name

def _clean():
	for i in [MONITOR_PORT, FS_PORT, MDS_PORT]:
		close_port(i)

def create(addr):
	_clean()
	Monitor().start()
	virthub.create(addr2name(addr))

def attach(addr, key=None):
	virthub.start(addr2name(addr), key)

def detach(addr):
	virthub.remove(addr2name(addr))
