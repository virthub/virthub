import os
import sys
import shlex
import shutil
import argparse
import platform
from commands import getoutput
from subprocess import check_output, call

FS = {'matefs':'metafs'}
TOOLS = {'dump':'dump'}
MODULES = {'ckpt':'ckpt.ko', 'klnk':'klnk', 'vmap':'vmap'}

KERN_SRC = '/usr/src'
DEVNULL = open(os.devnull, 'wb')

_args = {}
_scripts = {'run':'python'}
_dirs = ['modules', 'fs', 'tools']

_fs = []
_tools = []
_modules = {}

def get_home_dir():
    name = platform.system()
    if name == 'Linux':
        readlink = 'readlink'
    else:
        raise Exception('%s is not supported.' % name)
    f = check_output([readlink, '-f', sys.argv[0]])
    d = os.path.dirname(f)
    return os.path.dirname(d)

_home = get_home_dir()
sys.path.append(os.path.join(_home, 'conf'))
sys.path.append(os.path.join(_home, 'scripts'))
from defaults import *

if MATEFS:
    _fs.append(FS['matefs'])
if DUMP:
    _tools.append(TOOLS['dump'])
if KLNK:
    _modules.update({'klnk':MODULES['klnk']})
if CKPT:
    _modules.update({'ckpt':MODULES['ckpt']})
if VMAP:
    _modules.update({'vmap':MODULES['vmap']})

def get_conf():
    return os.path.join(_home, 'conf', 'build.cfg')

def generate():
    dest = os.path.join(_home, 'bin')
    if not os.path.isdir(dest):
        os.makedirs(dest, 0o755)
    dirname = os.path.join(_home, 'scripts')
    for i in _scripts:
        path = os.path.join(dirname, "%s.py" % i)
        if not os.path.exists(path):
            raise Exception('Error: failed to find %s' % path)
        filename = os.path.join(dest, i)
        shutil.copyfile(path, filename)
        with open(filename) as f:
            lines = f.readlines()
        path = check_output(['which', _scripts[i]])
        if not path:
            raise Exception('Error: failed to find %s' % _scripts[i])
        lines.insert(0, "#!%s\n" % path)
        with open(filename, 'w') as f:
            f.writelines(lines)
        check_output(['chmod', '+x', filename])

def chkconf():
    name = platform.system()
    if name not in PLAT:
        raise Exception('Error: %s is not supported' % name)

    if not INFO.has_key('tools'):
        INFO['tools'] = {}

    if not INFO.has_key('version'):
        INFO['version'] = '0.0.0'

    if not INFO['name']:
        INFO['name'] = os.path.basename(_home)

def chktools():
    for i in DEPS:
        if i and not check_output(['which', i]):
            raise Exception('cannot find %s' %i)

def chkargs():
    path = get_conf()
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

def chkkern():
    path = os.path.join(KERN_SRC, 'linux-%s' % _args['kernel'])
    if not os.path.exists(path):
        raise Exception('Error: failed to find %s' % path)
    link = os.path.join(KERN_SRC, 'linux')
    if not os.path.exists(link):
        os.system("ln -s %s %s" % (path, link))
    else:
        res = getoutput('readlink -f %s' % link)
        if res != path:
            raise Exception('Error: the default path of linux kernel is invalid (the path %s is linked to %s)' % (res, link))
    modules = '/lib/modules/%s/build' % _args['kernel']

def invoke(cmd, path=None, quiet=False, ignore=False):
    if path:
        os.chdir(path)
    if not quiet:
        check_output(cmd, shell=True)
    else:
        if call(shlex.split(cmd), stdout=DEVNULL, stderr=DEVNULL):
            if not ignore:
                raise Exception('Error: failed to run %s' % cmd)

def install_packages():
    for i in PKG:
        if i:
            cmd = 'apt-get install -y %s' % i
            invoke(cmd)
    for i in PYLIB:
        if i:
            cmd = 'pip install %s --upgrade' % i
            invoke(cmd)

def install_bootstrap():
    path = PATH_LXC_ROOT
    if os.path.isdir(path):
        shutil.rmtree(path)
    os.makedirs(path, 0o755)
    cmd = 'debootstrap --arch %s %s %s %s' % (_args['arch'], _args['release'], path, MIRROR)
    invoke(cmd)

def install():
    if int(_args['packages']):
        install_packages()
    if int(_args['bootstrap']):
        install_bootstrap()

def configure():
    chkargs()
    chkconf()
    chktools()
    chkkern()

def build(args=None, quiet=False):
    for i in _dirs:
        dirname = os.path.join(_home, i)
        for j in os.listdir(dirname):
            if j not in _modules and j not in _fs and j not in _tools:
                continue
            path = os.path.join(dirname, j)
            if not path.startswith('.') and os.path.isdir(path):
                if j in _args and not int(_args[j]):
                    continue
                filename = os.path.join(path, 'build.sh')
                if os.path.exists(filename):
                    if not quiet:
                        print("|--> %s/%s" % (i, j))
                    if not args:
                        invoke('./build.sh', path)
                    else:
                        invoke('./build.sh %s' % args, path)

def setup():
    paths = [PATH_BIN, PATH_RUN, PATH_VAR, PATH_INIT, PATH_MNT, PATH_CONF, PATH_KLNK, PATH_LOG, PATH_ROOT]
    for path in paths:
        os.system("mkdir -p %s" % path)
        if path == PATH_INIT:
            for name in _modules:
                module = os.path.join(_home, 'modules', name, 'build', _modules[name])
                if not os.path.exists(module):
                    raise Exception('failed to find %s' % module)
                shutil.copy2(module, os.path.join(path, _modules[name]))
            for name in _fs:
                fs = os.path.join(_home, 'fs', name, 'build', name)
                if not os.path.exists(fs):
                    raise Exception('failed to find %s' % fs)
                shutil.copy2(fs, os.path.join(path, name))
            for name in _tools:
                tool = os.path.join(_home, 'tools', name, 'build', name)
                if not os.path.exists(tool):
                    raise Exception('failed to find %s' % tool)
                shutil.copy2(tool, os.path.join(path, name))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--clean', action='store_true')
    args = parser.parse_args(sys.argv[1:])
    if args.clean:
        build('--clean', quiet=True)
    else:
        generate()
        configure()
        install()
        build()
        setup()
