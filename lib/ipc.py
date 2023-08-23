import os
import time
import shutil
import psutil
from lib import fs
from lib.log import log
from defaults import PATH_RUN, PATH_MNT, PATH_INIT, PATH_LXC_LIB, IPC
from lib.util import chkpid, kill_by_file, umount, name2addr, save_pid, bind, mkdirs, popen, rmdir

PRINT = True
RETRY_MAX = 10
RETRY_INTERVAL = 0.5 # sec
KLNK_NAME = 'klnk'

def _log(text):
    if PRINT:
        log('ipc: ' + text)

def get_name():
    return KLNK_NAME

def get_mountpoint(name):
    return os.path.join(PATH_MNT, KLNK_NAME, name)

def get_binding(name):
    return os.path.join(fs.get_mnt(name), KLNK_NAME)

def get_pid(name):
    path = _get_path_run(name)
    if os.path.exists(path):
        with open(path, 'r') as f:
            return int(f.readlines()[0].strip())

def _get_path_run(name):
    return os.path.join(PATH_RUN, '%s.%s.pid' % (KLNK_NAME, name))

def _get_path_cmd():
    return os.path.join(PATH_INIT, KLNK_NAME)

def _touch(path):
    for _ in range(RETRY_MAX):
        if os.path.exists(path):
            return True
        time.sleep(RETRY_INTERVAL)

def _start(name):
    _log('start, name=%s' % name)
    path = _get_path_run(name)
    kill_by_file(KLNK_NAME, path)

    binding = get_binding(name)
    umount(binding)
    mkdirs(binding)

    mountpoint = get_mountpoint(name)
    umount(mountpoint)
    mkdirs(mountpoint)

    cmd = [_get_path_cmd(), name2addr(name), mountpoint]
    # _log(' '.join(cmd))
    pid = popen(cmd)
    if not chkpid(pid):
        raise Exception("failed to start ipc")
    path = _get_path_run(name)
    save_pid(path, pid)
    path = os.path.join(mountpoint, 'test')
    if not _touch(path):
        raise Exception('failed to start ipc')
    bind(mountpoint, binding)
    path = os.path.join(binding, 'test')
    if not _touch(path):
        raise Exception('failed to start ipc')

def _release(name):
    path = _get_path_run(name)
    kill_by_file(path)

    binding = get_binding(name)
    umount(binding)

    mountpoint = get_mountpoint(name)
    umount(mountpoint)
    rmdir(mountpoint)

def create(name):
    if IPC:
        _start(name)

def start(name):
    if IPC:
        _start(name)

def stop(name):
    if IPC:
        _release(name)

def remove(name):
    if IPC:
        _release(name)
