# container.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import shlex
import random
import shutil
import tempfile
import subprocess
from core.lib.log import log
from core.utils.service import fs
from core.utils.service import ipc
from core.utils.service import edge
from core.utils.service import ckpt
from commands import getstatusoutput
from core.lib.util import DIR_MODE, ifaddr
from core.conf.env import PATH_LXC_LIB, PATH_LXC_ROOT, PATH_BIN, PATH_MIFS, PATH_VMAP
from core.conf.defaults import DEBUG, PROBE, MIFS, CKPT, AA_ALLOW_INCOMPLETE, AA_PROFILE_UNCONFINED, BR_NAME, PROBE_PORT

SSH = True
PRINT = True
SILENCE = False
TERMINAL = False

def _log(text):
	if PRINT:
		log('container: ' + text)

def _check_hwaddr():
	return "00:16:3e:" + ":".join(['%02x' % random.randint(0, 255) for _ in range(3)])

def _check_cfg(name):
	dirname = os.path.join(PATH_LXC_LIB, name)
	if not os.path.isdir(dirname):
		os.makedirs(dirname)
	cfg = []
	if PROBE:
		cfg.append('lxc.network.type = veth\n')
		cfg.append('lxc.network.link = %s\n' % BR_NAME)
		cfg.append('lxc.network.flags = up\n')
		cfg.append('lxc.network.hwaddr = %s\n' % _check_hwaddr())
	cfg.append('lxc.rootfs = %s\n' % os.path.join(dirname, 'rootfs'))
	cfg.append('lxc.mount = %s\n' % os.path.join(dirname, 'fstab'))
	cfg.append('lxc.utsname = %s\n' % name)
	cfg.append('lxc.devttydir = lxc\n')
	cfg.append('lxc.tty = 4\n')
	cfg.append('lxc.pts = 1024\n')
	cfg.append('lxc.arch = i686\n')
	cfg.append('lxc.cap.drop = sys_module mac_admin\n')
	if AA_ALLOW_INCOMPLETE:
		cfg.append('lxc.aa_allow_incomplete = 1\n')
	if AA_PROFILE_UNCONFINED:
		cfg.append('lxc.aa_profile = unconfined\n')
	#cfg.append('lxc.pivotdir = lxc_putold\n')
	cfg.append('lxc.cgroup.devices.deny = a\n')
	cfg.append('lxc.cgroup.devices.allow = c *:* m\n')
	cfg.append('lxc.cgroup.devices.allow = b *:* m\n')
	cfg.append('lxc.cgroup.devices.allow = c 1:3 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 1:5 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 5:1 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 5:0 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 1:9 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 1:8 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 136:* rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 5:2 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 254:0 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 10:229 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 10:200 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 1:7 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 10:228 rwm\n')
	cfg.append('lxc.cgroup.devices.allow = c 10:232 rwm\n')

	path = os.path.join(dirname, 'config')
	with open(path, 'w') as f:
		f.writelines(cfg)

def _check_fstab(name, attach=False):
	fstab = []
	fstab.append('proc            proc         proc    nodev,noexec,nosuid 0 0\n')
	fstab.append('sysfs           sys          sysfs defaults  0 0\n')

	binding = ipc.get_binding(name)
	mountpoint = ipc.get_mountpoint(name)
	if not os.path.isdir(binding):
		os.makedirs(binding, DIR_MODE)
	fstab.append('%s      %s       none   bind  0 0\n' % (mountpoint, binding))

	binding = fs.get_binding(name)
	mountpoint = fs.get_mountpoint(name)
	if not attach:
		src = mountpoint
		dest = binding
		if not os.path.isdir(dest):
			os.makedirs(dest, DIR_MODE)
		fstab.append('%s      %s       none   bind  0 0\n' % (src, dest))

		dest = os.path.join(binding, 'bin')
		if not os.path.isdir(dest):
			os.makedirs(dest, DIR_MODE)
		fstab.append('%s      %s       none   bind  0 0\n' % (PATH_BIN, dest))
	else:
		if not os.path.isdir(binding):
			os.makedirs(binding, DIR_MODE)
		fstab.append('%s      %s       none   bind  0 0\n' % (mountpoint, binding))

	if MIFS:
		path = os.path.join(PATH_LXC_LIB, name, 'rootfs', PATH_MIFS[1:])
		if not os.path.isdir(path):
			os.makedirs(path, DIR_MODE)
		fstab.append('%s      %s       none   bind  0 0\n' % (PATH_MIFS, path))

	if CKPT:
		path = os.path.join(PATH_LXC_LIB, name, 'rootfs', PATH_VMAP[1:])
		if not os.path.isdir(path):
			os.makedirs(path, DIR_MODE)
		src = ckpt.get_path_vmap(name)
		fstab.append('%s      %s       none   bind  0 0\n' % (src, path))

	path = os.path.join(PATH_LXC_LIB, name, 'fstab')
	with open(path, 'w') as f:
		f.writelines(fstab)

def _check_root(name):
	path = os.path.join(PATH_LXC_LIB, name, 'rootfs')
	os.system('cp -ax --reflink=always %s/. %s' % (PATH_LXC_ROOT, path))

def _check_probe(name):
	addr = ifaddr(BR_NAME)
	port = PROBE_PORT + len(os.listdir(PATH_LXC_LIB))
	dirname = tempfile.mkdtemp()
	try:
		rsa = os.path.join(dirname, 'id_rsa')
		os.system('ssh-keygen -t rsa -P "" -f %s 1>/dev/null' % rsa)
		os.system('cat %s>>/root/.ssh/authorized_keys' % rsa + '.pub')
		path = os.path.join(PATH_LXC_LIB, name, 'rootfs', 'root/.ssh')
		if not os.path.isdir(path):
			os.makedirs(path, DIR_MODE)
		shutil.move(rsa, os.path.join(path, 'id_rsa'))
		shutil.move(rsa + '.pub', os.path.join(path, 'id_rsa.pub'))
	finally:
		shutil.rmtree(dirname)

	script = []
	script.append('#!/bin/sh\n')
	script.append('ssh -o StrictHostKeyChecking=no -Nf -R %d:localhost:22 root@%s\n' % (port, addr))
	script.append('exit 0\n')

	path = os.path.join(PATH_LXC_LIB, name, 'rootfs', 'etc', 'rc.local')
	with open(path, 'w') as f:
		f.writelines(script)

def _check_ssh():
	filename = './.rsa'
	if not os.path.exists(filename):
		dirname = tempfile.mkdtemp()
		try:
			rsa = os.path.join(dirname, 'id_rsa')
			os.system('ssh-keygen -t rsa -P "" -f %s 1>/dev/null' % rsa)
			res, _ = getstatusoutput('ls ~/.ssh')
			if res:
				os.system('mkdirs -p ~/.ssh')
			os.system("cp %s %s ~/.ssh" % (rsa, rsa + '.pub'))
			os.system('cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys')
			os.system('touch %s' % filename)
		finally:
			shutil.rmtree(dirname)

def _start_container(name):
	pid = ipc.get_pid(name)
	if not pid:
		log('failed to start container')
		raise Exception('failed to start container')
	path = os.path.join(PATH_LXC_LIB, name, 'config')
	cmd = 'lxc-start -n %s -f %s --share-ipc %d' % (name, path, pid)
	_log('%s' % cmd)
	if not SILENCE:
		if TERMINAL:
			os.system("gnome-terminal --command='%s -F'" % cmd)
		else:
			if SSH:
				_check_ssh()
				cmd = 'ssh -o StrictHostKeyChecking=no localhost "%s -F"' % cmd
			subprocess.Popen(shlex.split(cmd))
	os.system('lxc-wait -n %s -s "RUNNING"' % name)
	os.system('lxc-device add -n %s /dev/ckpt' % name)

def _stop(name):
	os.system('lxc-stop -n %s 2>/dev/null' % name)

def _remove(name):
	path = os.path.join(PATH_LXC_LIB, name, 'rootfs')
	if os.path.exists(path):
		os.system('btrfs subvolume delete %s 2>/dev/null' % path)
	path = os.path.join(PATH_LXC_LIB, name)
	if os.path.exists(path):
		shutil.rmtree(path)

def _release(name):
	path = os.path.join(PATH_LXC_LIB, name)
	if os.path.isdir(path):
		try:
			_stop(name)
			fs.stop(name)
			ipc.stop(name)
			edge.stop(name)
			_remove(name)
		except:
			pass

def _release_probe():
	os.system('rm ~/.ssh/known_hosts 2>/dev/null')

def _release_mifs():
	os.system('killall -9 mifs 2>/dev/null')
	os.system('umount %s 2>/dev/null' % PATH_MIFS)

def clean():
	if PROBE:
		_release_probe()

	for name in os.listdir(PATH_LXC_LIB):
		_release(name)

	if MIFS:
		_release_mifs()

def _start(name, attach=False):
	_log('checking cfg')
	_check_cfg(name)

	_log('checking fstab')
	_check_fstab(name, attach)

	_log('checking root')
	_check_root(name)

	if PROBE:
		_log('checking probe')
		_check_probe(name)

	_start_container(name)

def start(name):
	_log('start, name=%s' % name)
	_start(name, attach=True)

def create(name):
	_start(name)

def stop(name):
	_release(name)

def remove(name):
	_release(name)
