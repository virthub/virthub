import os
import sys
import shlex
import shutil
import platform
from subprocess import check_output, call

DEVNULL = open(os.devnull, 'wb')

def get_home_dir():
    name = platform.system()
    if name == 'Linux':
        readlink = 'readlink'
    else:
        raise Exception('%s is not supported.' % name)
    f = check_output([readlink, '-f', sys.argv[0]])
    d = os.path.dirname(f)
    return os.path.dirname(d)

sys.path.append(os.path.join(get_home_dir(), 'conf'))
from defaults import *

def get_conf():
    home = get_home_dir()
    return os.path.join(home, 'conf', 'build.cfg')

def get_build_dir():
    home = get_home_dir()
    return os.path.join(home, 'build')

def get_system_map():
    ver = os.uname()[2]
    if ver != PLAT[1]:
        raise Exception('Error: kernel version is %s, %s is required' % (ver, PLAT[1]))
    path = "/boot/System.map-%s" % ver
    if not os.path.exists(path):
        raise Exception('Error: failed to find %s' % path)
    return path

def get_syscall_addr(syscall, buf):
    for line in buf:
        if syscall in line:
            return line.split(' ')[0]

def gen_sysmap():
    dirname = os.path.join(get_build_dir(), 'src')
    name = get_system_map()
    with open(name) as f:
        buf = f.readlines()
    lines = ['#ifndef _SYSMAP_H\n']
    lines.append('#define _SYSMAP_H\n\n')
    for i in SYSCALLS:
        addr = get_syscall_addr(i, buf)
        if not addr:
            raise Exception('Error: failed to find of %s' % i)
        lines.append("#define %s 0x%s\n" % (i.upper(), addr))
    lines.append('\n#endif\n')
    path = os.path.join(dirname, 'sysmap.h')
    with open(path, 'w') as f:
        f.writelines(lines)

def copy_dir(dir_src, dir_dest):
    if not os.path.isdir(dir_dest):
        os.makedirs(dir_dest, 0o755)
    for i in os.listdir(dir_src):
        src = os.path.join(dir_src, i)
        dest = os.path.join(dir_dest, i)
        if os.path.isdir(src):
            copy_dir(src, dest)
        else:
            shutil.copy(src, dest)

def generate():
    home = get_home_dir()
    build = get_build_dir()
    dir_dest = os.path.join(build, 'src')
    dir_src = os.path.join(home, 'src')
    copy_dir(dir_src, dir_dest)
    gen_sysmap()

def chk_conf():
    name = platform.system()
    if name not in PLAT:
        raise Exception('Error: %s is not supported' % name)

    if not INFO.has_key('version'):
        INFO['version'] = '0.0.0'

    if not INFO['name']:
        INFO['name'] = os.path.basename(get_home_dir())

def get_obj_list(head, files):
    cnt = 0
    for i in files:
        if cnt % 8 == 7:
            head += '\\\n\t'
        head += '%s ' % i
        cnt += 1
    return head + '\n'

def get_obj_files():
    files = []
    dirname = os.path.join(get_build_dir(), 'src')
    for i in os.listdir(dirname):
        path = os.path.join(dirname, i)
        if i.endswith('.c'):
            files.append(os.path.join('src', i[:-2] + '.o'))
    return files

def chk_proj():
    lines = []
    lines.append('ifneq ($(KERNELRELEASE),)\n\n')
    lines.append('obj-m := %s.o\n' % INFO['name'])
    objs = '%s-objs := ' % INFO['name']
    files = get_obj_files()
    lines.append(get_obj_list(objs, files))
    cflags = 'EXTRA_CFLAGS :='
    if DEFS:
        cflags += ' %s' % ' '.join(['-D%s' % i for i in DEFS])
    lines.append(cflags)
    lines.append('\n')
    lines.append('else\n\n')
    lines.append('KERNELDIR := /lib/modules/$(shell uname -r)/build\n')
    lines.append('PWD := $(shell pwd)\n\n')
    lines.append('all:\n')
    lines.append('\tmake modules\n\n')
    lines.append('modules:\n')
    lines.append('\t$(MAKE) -C $(KERNELDIR) M=$(PWD) modules\n')
    lines.append('endif\n\n')
    lines.append('depend .depend dep:\n')
    lines.append('\t$(CC) $(CFLAGS) -M *.c > .depend\n\n')
    lines.append('ifeq (.depend,$(wildcard .depend))\n')
    lines.append('include .depend\n')
    lines.append('endif\n')

    path = os.path.join(get_build_dir(), 'Makefile')
    with open(path, 'w') as f:
        f.writelines(lines)

def read_params():
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
            if key not in PARAMS:
                raise Exception('Error: cannot find the definition of %s' % key)
            PARAMS[key] = val

def chk_params():
    read_params()
    for i in PARAMS:
        res = PARAMS[i]
        if None == res:
            raise Exception('Error: %s is not set' % i)
        val = ''
        typ = type(PARAMS[i])
        if typ == type(True):
            val = i.upper()
            if not PARAMS[i] or val in DEFS:
                continue
        elif typ == str:
            val = '%s=\'"%s"\'' % (i.upper(), PARAMS[i])
        elif typ == int:
            val = '%s=%s' % (i.upper(), str(res))
        else:
            raise Exception('Error: invalid type of %s' % i)
        DEFS.append(val)

def configure():
    chk_params()
    chk_conf()
    chk_proj()

def invoke(cmd, path=None, quiet=False, ignore=False):
    if path:
        os.chdir(path)
    if not quiet:
        check_output(cmd, shell=True)
    else:
        if call(shlex.split(cmd), stdout=DEVNULL, stderr=DEVNULL):
            if not ignore:
                raise Exception('Error: failed to run %s' % cmd)

def build():
    invoke('make', get_build_dir())

if __name__ == '__main__':
    generate()
    configure()
    build()
