# virthub.py
#
# Copyright (C) 2019 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
from lib.log import log
from services.capsule import Capsule
from services.storage import Storage
from services.network import Network
from lib import fs, ipc, ckpt, container
from lib.util import umount, mkdirs, rmdir
from conf.defaults import PATH_RUN, PATH_VAR, PATH_BIN, PATH_MNT, PATH_LOG

PRINT = True

_services = [Network(), Storage(), Capsule()]

def _log(text):
    if PRINT:
        log('vhub: ' + text)

def _check_path(name):
    mkdirs(PATH_RUN)
    os.system('rm -f %s' % os.path.join(PATH_RUN, '%s.%s.pid' % (container.LXC_NAME, name)))
    os.system('rm -f %s' % os.path.join(PATH_RUN, '%s.%s.pid' % (ipc.KLNK_NAME, name)))
    os.system('rm -f %s' % os.path.join(PATH_RUN, '%s.%s.pid' % (fs.FS_NAME, name)))
    os.system('rm -f %s' % os.path.join(PATH_RUN, '%s.%s.pid' % (ckpt.VMAP_NAME, name)))
    mkdirs(PATH_MNT)
    mkdirs(os.path.join(PATH_MNT, fs.get_name()))
    mkdirs(os.path.join(PATH_MNT, ipc.get_name()))
    mkdirs(os.path.join(PATH_MNT, ckpt.get_name()))
    mkdirs(PATH_BIN)
    mkdirs(PATH_VAR)
    mkdirs(PATH_LOG)
    os.system('rm -f %s' % os.path.join(PATH_LOG, '%s.%s.log' % (container.LXC_NAME, name)))
    os.system('rm -f %s' % os.path.join(PATH_LOG, '%s.%s.log' % (ipc.KLNK_NAME, name)))
    os.system('rm -f %s' % os.path.join(PATH_LOG, '%s.%s.log' % (fs.MATEFS_NAME, name)))
    mkdirs(fs.get_conf_path())
    dirname = fs.get_path(name)
    umount(dirname)
    mkdirs(dirname)

def _init(name):
    _check_path(name)
    for item in _services:
        item.clean(name)

def create(name):
    _log('create, name=%s' % name)
    _init(name)
    for item in _services:
        item.create(name)
    _log('finish creating %s' % name)

def start(name, key=None):
    _log('start, name=%s' % name)
    _check_path(name)
    for item in _services:
        item.start(name, key)
    _log('finish starting %s' % name)

def stop(name):
    _log('stop, name=%s' % name)
    for item in _services:
        item.stop(name)
    _log('finish stopping %s' % name)

def remove(name):
    _log('remove, name=%s' % name)
    for item in _services:
        item.remove(name)
    _log('finish removing %s' % name)
