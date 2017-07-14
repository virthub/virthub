# build.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import sys
import wget
import shlex
import shutil
import tarfile
import platform
from subprocess import check_output, call

DEVNULL = open(os.devnull, 'wb')

_defs = {}

def _get_home_dir():
    name = platform.system()
    if name == 'Linux':
        readlink = 'readlink'
    else:
        raise Exception('%s is not supported.' % name)
    f = check_output([readlink, '-f', sys.argv[0]])
    d = os.path.dirname(f)
    return os.path.dirname(d)

sys.path.append(os.path.join(_get_home_dir(), 'scripts'))
from configure import *

def _get_conf():
    home = _get_home_dir()
    return os.path.join(home, 'build.cfg')

def _get_inc():
    home = _get_home_dir()
    return os.path.join(home, 'include')

def _get_patch():
    home = _get_home_dir()
    return os.path.join(home, 'src', 'linux-%s.patch' % _defs['kernel_version'])

def _get_kernel_config():
    home = _get_home_dir()
    return os.path.join(home, 'conf', 'config-%s' % _defs['kernel_version'])

def _get_url():
    ver = _defs['kernel_version']
    major, _, _ = ver.split('.')
    return 'https://www.kernel.org/pub/linux/kernel/v%s.x/linux-%s.tar.gz' % (major, ver)

def _get_path():
    return os.path.join(_defs['path_source'], 'linux-%s' % _defs['kernel_version'])

def _read_args():
    path = _get_conf()
    with open(path) as f:
        lines = f.readlines()

    for i in lines:
        i = i.strip()
        if i and not i.startswith('#'):
            res = i.split('=')
            if len(res) != 2:
                raise Exception('Error: failed to parse %s' % i)
            key = res[0].lower()
            val = res[1].split('#')[0].strip()
            if key not in ARGS:
                raise Exception('Error: cannot find the definition of %s' % key)
            ARGS[key]['value'] = val

def _chkargs():
    _read_args()
    for i in ARGS:
        res = ARGS[i].get('value')
        if None == res:
            res = ARGS[i].get('default')
            if None == res:
                raise Exception('Error: %s is not set' % i)
        _defs[i] = str(res)

def _config():
    src = _get_kernel_config()
    if not os.path.exists(src):
        raise Exception('Error: failed to find config-%s' % _defs['kernel_version'])
    dest = os.path.join(_get_path(), '.config')
    shutil.copyfile(src, dest)

def _include():
    dir_src = _get_inc()
    dir_dest = os.path.join(_get_path(), 'include', 'linux')
    for i in os.listdir(dir_src):
        if i.endswith('.h'):
            src = os.path.join(dir_src, i)
            dest = os.path.join(dir_dest, i)
            shutil.copyfile(src, dest)

def _download():
    url = _get_url()
    name = os.path.join(_defs['path_source'], 'linux-%s.tar.gz' % _defs['kernel_version'])
    if os.path.exists(name):
        os.remove(name)
    wget.download(url, name)
    path = _get_path()
    if os.path.exists(path):
        shutil.rmtree(path)
    tar = tarfile.open(name)
    tar.extractall(_defs['path_source'])
    tar.close()
    os.remove(name)

def _patch():
    _download()
    cmd = 'patch -d %s -p1 < %s' % (_get_path(), _get_patch())
    os.system(cmd)

def _configure():
    _chkargs()
    _patch()
    _config()
    _include()

def _call(cmd, path=None, quiet=False, ignore=False):
    if path:
        os.chdir(path)
    if not quiet:
        check_output(cmd, shell=True)
    else:
        if call(shlex.split(cmd), stdout=DEVNULL, stderr=DEVNULL):
            if not ignore:
                raise Exception('Error: failed to run %s' % cmd)

def _build():
    path = _get_path()
    _call('make', path)
    _call('make modules', path)
    _call('make module_install', path)
    _call('make install', path)

if __name__ == '__main__':
    _configure()
    _build()
