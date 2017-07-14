# build.py
#
# Copyright (C) 2017 Yi-Wei Ci
#
# Distributed under the terms of the MIT license.
#

import os
import sys
import shlex
import shutil
import argparse
import platform
from subprocess import check_output, call

_args = {}
_dirs = ['sys', 'tools']
_bin = {'login':'python3', 'start':'python'}

DEVNULL = open(os.devnull, 'wb')

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

def _generate():
    home = _get_home_dir()
    dest = os.path.join(home, 'bin')
    if not os.path.isdir(dest):
        os.makedirs(dest, 0o755)
    dirname = os.path.join(home, 'scripts')
    for i in _bin:
        path = os.path.join(dirname, "%s.py" % i)
        if not os.path.exists(path):
            raise Exception('Error: failed to find %s' % path)
        filename = os.path.join(dest, i)
        shutil.copyfile(path, filename)
        with open(filename) as f:
            lines = f.readlines()
        path = check_output(['which', _bin[i]])
        if not path:
            raise Exception('Error: failed to find %s' % _bin[i])
        lines.insert(0, "#!%s\n" % path)
        with open(filename, 'w') as f:
            f.writelines(lines)
        check_output(['chmod', '+x', filename])

def _chkconf():
    name = platform.system()
    if name not in PLAT:
        raise Exception('Error: %s is not supported' % name)

    if not INFO.has_key('tools'):
        INFO['tools'] = {}

    if not INFO.has_key('version'):
        INFO['version'] = '0.0.0'

    if not INFO['name']:
        INFO['name'] = os.path.basename(_get_home_dir())

def _chktools():
    for i in DEPS:
        if not check_output(['which', i]):
            raise Exception('cannot find %s' %i)

def _chkargs():
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
            _args[key] = str(val)

def _call(cmd, path=None, quiet=False, ignore=False):
    if path:
        os.chdir(path)
    if not quiet:
        check_output(cmd, shell=True)
    else:
        if call(shlex.split(cmd), stdout=DEVNULL, stderr=DEVNULL):
            if not ignore:
                raise Exception('Error: failed to run %s' % cmd)

def _install_packages():
    for i in APT:
        cmd = 'apt-get install -y %s' % i
        _call(cmd)
    for i in PIP:
        cmd = 'pip install %s --upgrade' % i
        _call(cmd)
    path = os.path.join(_args['lxc_cache'], _args['release'])
    if os.path.isdir(path):
        shutil.rmtree(path)
    os.makedirs(path, 0o755)

def _install_bootstrap():
    cmd = 'debootstrap --arch %s %s %s %s' % (_args['arch'], _args['release'], path, _args['mirror'])
    _call(cmd)

def _install():
    if int(_args['packages']):
        _install_packages()
    if int(_args['bootstrap']):
        _install_bootstrap()

def _configure():
    _chkargs()
    _chkconf()
    _chktools()

def _build(args=None, quiet=False):
    home = _get_home_dir()
    for i in _dirs:
        dirname = os.path.join(home, i)
        for j in os.listdir(dirname):
            path = os.path.join(dirname, j)
            if not path.startswith('.') and os.path.isdir(path):
                if j in _args and not int(_args[j]):
                    continue
                filename = os.path.join(path, 'build.sh')
                if os.path.exists(filename):
                    if not quiet:
                        print("|--> %s/%s" % (i, j))
                    if not args:
                        _call('./build.sh', path)
                    else:
                        _call('./build.sh %s' % args, path)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--clean', action='store_true')
    args = parser.parse_args(sys.argv[1:])
    if args.clean:
        _build('--clean', quiet=True)
    else:
        _generate()
        _configure()
        _install()
        _build()
