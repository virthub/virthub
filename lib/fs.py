import os
import sys
import subprocess
from lib import ftp
import multiprocessing
from defaults import *
from lib.log import log
from lib.util import name2addr, close_port, ifaddr, umount, chkpid, save_pid, bind, kill_by_file, mkdirs, popen, umount, cpdir

PRINT = True
FS_NAME = 'matefs'

def _log(text):
    if PRINT:
        log('fs: %s' % text)

def get_name():
    return FS_NAME

def get_conf_path():
    return os.path.join(PATH_VAR, 'conf')

def get_path(name):
    return os.path.join(PATH_VAR, 'cache', name)

def get_binding(name):
    return os.path.join(PATH_LXC_LIB, name, 'rootfs', 'vhub', 'root')

def _release(name):
    umount(get_binding(name))
    umount(get_path(name))

def _get_path_run(name):
    return os.path.join(PATH_RUN, '%s.%s.pid' % (FS_NAME, name))

def _get_path_home(name):
    return os.path.join(get_path(name), FS_NAME)

def _get_root(name):
    return os.path.join(get_path(name), 'root')

def _get_bin(name):
    return os.path.join(_get_root(name), 'bin')

def _get_fs_mnt(name):
    return os.path.join(PATH_MNT, FS_NAME, name)

def get_mountpoint(name, fs=False):
    if not fs:
        return _get_root(name)
    else:
        return _get_fs_mnt(name)

def get_mnt(name):
    dirname = os.path.join(PATH_LXC_LIB, name, 'rootfs')
    mnt = '/'.join(PATH_MNT.split('/')[1:])
    return os.path.join(dirname, mnt)

def _get_path_cmd():
    return os.path.join(PATH_INIT, FS_NAME)

def _start_fs(name, create=False):
    mnt = _get_fs_mnt(name)
    path = _get_path_run(name)
    home = _get_path_home(name)
    _log('start fs, name=%s, home=%s, mnt=%s' % (name, home, mnt))
    kill_by_file(FS_NAME, path)
    umount(mnt)
    mkdirs(mnt)
    if create:
        mkdirs(home)
    cmd = [_get_path_cmd(), name, home, mnt]
    # _log(' '.join(cmd))
    pid = popen(cmd)
    if not chkpid(pid):
        log('failed to start fs (name=%s)' % name)
        raise Exception('failed to start fs')
    save_pid(path, pid)

def create(name):
    dirname = get_path(name)
    _log('create, mnt=%s' % dirname)
    mkdirs(dirname)
    path = _get_bin(name)
    mkdirs(path)
    cpdir(PATH_BIN, path)
    multiprocessing.Process(target=ftp.create, args=(dirname, name2addr(name), FS_PORT)).start()
    if FS:
        _start_fs(name, create=True)

def start(name):
    path = get_path(name)
    _log('start, mnt=%s' % path)
    mkdirs(path)
    umount(path)
    ftp.mount(path, name2addr(name), FS_PORT)
    path = _get_bin(name)
    if not os.path.exists(path):
        log('failed to start (name=%s)' % name)
        raise Exception('Error: failed to start (name=%s)' % name)
    if FS:
        _start_fs(name)

def stop(name):
    _release(name)

def remove(name):
    _release(name)
