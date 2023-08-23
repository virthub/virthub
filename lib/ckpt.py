import os
import fs
import shutil
from lib.log import log
from commands import getoutput
from defaults import CKPT, PATH_INIT, PATH_RUN, PATH_MNT
from lib.util import chkpid, kill_by_file, umount, save_pid, mkdirs, popen, rmdir

PRINT = True
VMAP_NAME = 'vmap'
CKPT_NAME = 'ckpt'
CKPT_MODULE = CKPT_NAME + '.ko'

def _log(text):
    if PRINT:
        log('ckpt: %s' % text)

def _get_path_run(name):
    return os.path.join(PATH_RUN, '%s.%s.pid' % (VMAP_NAME, name))

def _get_path_home(name):
    return os.path.join(fs.get_path(name), CKPT_NAME)

def get_mountpoint(name):
    return os.path.join(PATH_MNT, VMAP_NAME, name)

def get_name():
    return VMAP_NAME

def _start(name, create=False):
    path = _get_path_run(name)
    kill_by_file(VMAP_NAME, path)
    vmap = os.path.join(PATH_INIT, VMAP_NAME)
    home = _get_path_home(name)
    if create:
        mkdirs(home)
    mountpoint = get_mountpoint(name)
    _log('start, home=%s, mnt=%s' % (home, mountpoint))
    umount(mountpoint)
    mkdirs(mountpoint)
    cmd = [vmap, home, mountpoint]
    pid = popen(cmd)
    if not chkpid(pid):
        log('failed to start ckpt')
        raise Exception('failed to start ckpt')
    path = _get_path_run(name)
    save_pid(path, pid)

def _release(name):
    path = _get_path_run(name)
    kill_by_file(VMAP_NAME, path)
    path = get_mountpoint(name)
    umount(path)
    rmdir(path)

def _has_mod():
    return getoutput("lsmod | grep %s" % CKPT_NAME).startswith(CKPT_NAME)

def _ins_mod():
    path = os.path.join(PATH_INIT, CKPT_MODULE)
    if not os.path.exists(path):
        raise Exception('Error: failed to find %s' % path)
    os.system('insmod %s' % path)

def create(name):
    if CKPT:
        if not _has_mod():
            _ins_mod()
        _start(name, create=True)

def start(name):
    if CKPT:
        if not _has_mod():
            _ins_mod()
        _start(name)

def stop(name):
    if CKPT:
        _release(name)

def remove(name):
    if CKPT:
        _release(name)
