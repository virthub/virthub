# backend.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import json
import time
import zerorpc
from core.lib.op import *
from json import dumps, loads
from defaults import BACKEND_PORT
from twisted.internet import reactor
from core.lib.log import log, log_err
from core.init import virtdev, virthub
from threading import Lock, Event, Thread
from core.lib.util import ifaddr, zmqaddr, addr2name
from core.conf.defaults import NOTIFIER_PORT, DAEMON_PORT, DEBUG
from autobahn.twisted.websocket import WebSocketServerProtocol, WebSocketServerFactory

PRINT = True

TIMEOUT = 10 # seconds
BUF_SIZE = 1024
QUEUE_MAX = 16
RETRY_MAX = 3
RETRY_INTERVAL = 5 # seconds

class Queue(object):
	def __init__(self):
		self._queue = {op: [] for op in Operations}
		self._locks = {op: Lock() for op in Operations}
		self._events = {op: Event() for op in Operations}
		for i in self._events:
			self._events[i].set()

	def push(self, op, buf):
		if op not in Operations or type(buf) != str or len(buf) > BUF_SIZE or not buf:
			return
		lock = self._locks[op]
		lock.acquire()
		try:
			queue = self._queue[op]
			if len(queue) == QUEUE_MAX:
				queue.pop(0)
			queue.append(buf)
			ev = self._events[op]
			if not ev.is_set():
				ev.set()
		except:
			log_err(self, 'failed')
		finally:
			lock.release()

	def pop(self, op):
		if op not in Operations:
			return
		lock = self._locks[op]
		lock.acquire()
		try:
			queue = self._queue[op]
			if len(queue) > 0:
				return queue.pop(0)
		except:
			log_err(self, 'failed')
		finally:
			lock.release()

	def wait(self, op, timeout=TIMEOUT):
		if op not in Operations:
			return
		lock = self._locks[op]
		lock.acquire()
		try:
			ev = self._events[op]
			if ev.is_set():
				ev.clear()
		finally:
			lock.release()
		ev.wait(timeout)

_queue = Queue()

class Worker(object):
	def push(self, op, buf):
		_queue.push(op, buf)
		return True

class Notifier(Thread):
	def run(self):
		srv = zerorpc.Server(Worker())
		srv.bind(zmqaddr('127.0.0.1', NOTIFIER_PORT))
		srv.run()

class Daemon(object):
	def __init__(self):
		self._cli = None

	def __enter__(self):
		cli = zerorpc.Client()
		cli.connect(zmqaddr('127.0.0.1', DAEMON_PORT))
		self._cli = cli
		return cli

	def __exit__(self, *args, **kwargs):
		if self._cli:
			self._cli.close()

class Backend(WebSocketServerProtocol):
	def __init__(self):
		self._init = False
		WebSocketServerProtocol.__init__(self)

	def _log(self, text):
		if PRINT:
			log('Backend: ' + text)

	def _login(self, user, password):
		self._log('login, user=%s' % user)
		virtdev.create(user, password)
		for _ in range(RETRY_MAX + 1):
			with Daemon() as d:
				node, addr = d.chknode()
			if addr:
				name = addr2name(addr)
				virthub.create(name)
				return dumps({'op':OP_LOGIN, 'result':{'node':node}})
			else:
				time.sleep(RETRY_INTERVAL)

	def _find(self, user, node):
		self._log('find, user=%s, node=%s' % (user, node))
		if user and node:
			with Daemon() as d:
				res = d.find(user, node)
				if res:
					return dumps({'op':OP_FIND, 'result':{'user':user, 'node':node, 'name':res['name']}})

	def _drop(self, name):
		self._log('drop, name=%s' % name)
		with Daemon() as d:
			if d.drop(name):
				return dumps({'op':OP_DROP, 'result':name})

	def _remove(self, name):
		self._log('remove, name=%s' % name)
		with Daemon() as d:
			addr, _ = d.chkaddr(name)
			if addr:
				virthub.remove(addr2name(addr))
				d.remove(name)
				return dumps({'op':OP_REMOVE, 'result':name})

	def _accept(self, user, node, name):
		self._log('accept, user=%s, node=%s, name=%s' % (user, node, name))
		with Daemon() as d:
			if d.accept(user, node, name):
				return dumps({'op':OP_ACCEPT, 'result':name})

	def _join(self, name):
		self._log('join, name=%s' % name)
		with Daemon() as d:
			if d.join(name):
				return dumps({'op':OP_JOIN, 'result':name})

	def _wait(self):
		self._log('wait')
		_queue.wait(OP_WAIT, timeout=0.5)
		buf = _queue.pop(OP_WAIT)
		if buf:
			res = json.loads(buf)
			if res:
				return dumps({'op':OP_WAIT, 'result':{'user':res['user'], 'node':res['node'], 'name':res['name']}})
		return dumps({'op':OP_WAIT, 'result':{}})

	def _connect(self, name):
		self._log('connect, name=%s' % name)
		with Daemon() as d:
			addr, key = d.chkaddr(name)
			virthub.start(addr2name(addr), key)

	def _list(self):
		self._log('list')
		_queue.wait(OP_LIST, timeout=0.5)
		buf = _queue.pop(OP_LIST)
		if buf:
			args = loads(buf)
			if args['state'] == 'join':
				self._connect(args['name'])
			with Daemon() as d:
				res = d.list()
				if res:
					return dumps({'op':OP_LIST, 'result':res})
		elif not self._init:
			self._init = True
			with Daemon() as d:
				res = d.list()
				if res:
					return dumps({'op':OP_LIST, 'result':res})
		return dumps({'op':OP_LIST, 'result':{}})

	def _proc(self, buf):
		args = loads(buf)
		if not args:
			log_err(self, 'no arguments')
			return
		op = args.pop('op')
		if op == OP_WAIT:
			return self._wait()
		elif op == OP_LIST:
			return self._list()
		elif op == OP_FIND:
			return self._find(**args)
		elif op == OP_JOIN:
			return self._join(**args)
		elif op == OP_DROP:
			return self._drop(**args)
		elif op == OP_LOGIN:
			return self._login(**args)
		elif op == OP_ACCEPT:
			return self._accept(**args)
		elif op == OP_REMOVE:
			return self._remove(**args)

	def _handle_request(self, payload, isBinary):
		buf = payload.decode('utf8')
		ret = self._proc(buf)
		if ret:
			self.sendMessage(ret, isBinary=False)

	def onMessage(self, payload, isBinary):
		if DEBUG:
			self._handle_request(payload, isBinary)
		else:
			try:
				self._handle_request(payload, isBinary)
			except:
				log_err(self, 'failed to process')

def _listen():
	url = "ws://%s:%d" % (ifaddr(), BACKEND_PORT)
	factory = WebSocketServerFactory(url=url)
	factory.protocol = Backend
	reactor.listenTCP(BACKEND_PORT, factory)
	reactor.run()

def start():
	notifier = Notifier()
	notifier.start()
	_listen()

if __name__ == '__main__':
	start()
