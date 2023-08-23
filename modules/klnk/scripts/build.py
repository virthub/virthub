import os
import sys
import shlex
import shutil
import platform
from subprocess import check_output, call

GDB = True
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

def get_syscall_table():
    if PLAT[3] != 'x86':
        raise Exception('Error: %s is not supported' % PLAT[3])
    if PLAT[2] == '32bit':
        return os.path.join(PARAMS['path_kernel'], 'arch/x86/entry/syscalls/', 'syscall_32.tbl')
    elif PLAT[2] == '64bit':
        return os.path.join(PARAMS['path_kernel'], 'arch/x86/entry/syscalls/', 'syscall_64.tbl')
    else:
        raise Exception('Error: unknown platform %s' % ' '.join(PLAT))

def get_syscall(name):
    if PLAT[2] == '32bit':
        return '__ia32_sys_%s' % name
    elif PLAT[2] == '64bit':
        return '__x64_sys_%s' % name
    else:
        raise Exception('Error: unknown platform %s' % ' '.join(PLAT))

def get_conf():
    home = get_home_dir()
    return os.path.join(home, 'conf', 'build.cfg')

def get_build_dir():
    home = get_home_dir()
    return os.path.join(home, 'build')

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

def get_system_map():
    ver = os.uname()[2]
    if ver != PLAT[1]:
        raise Exception('Error: kernel version is %s, %s is required' % (ver, PLAT[1]))
    path = '/boot/System.map-%s' % ver
    if not os.path.exists(path):
        raise Exception('Error: failed to find %s' % path)
    return path

def get_syscall_addr(syscall, buf):
    for line in buf:
        if syscall in line:
            return line.split(' ')[0]

def gen_sysmap():
    path_kernel = PARAMS.get('path_kernel')
    if not path_kernel or not os.path.exists(path_kernel):
        raise Exception('Error: %s dose not exist' % PARAMS['path_kernel'])
    with open(get_system_map()) as f:
        buf = f.readlines()
    for i in SYSCALLS:
        syscall = get_syscall(i)
        addr = get_syscall_addr(syscall, buf)
        if not addr:
            raise Exception('Error: failed to find of %s' % i)
    syscall_tab = get_syscall_table()
    with open(syscall_tab) as f:
        lines = f.readlines()
    syscalls = {}
    for i in SYSCALLS:
        matched = False
        for j in lines:
            if i in j:
                syscalls.update({i:j.split('\t')[0]})
                matched = True
        if not matched:
            raise Exception('Error: %s cannot be found in %s' % (i, syscall_tab))
    lines = ['#ifndef _SYSMAP_H\n']
    lines.append('#define _SYSMAP_H\n\n')
    for i in SYSCALLS:
        lines.append('#define __NR_%s %d\n' % (i.upper(), int(syscalls[i])))
    lines.append('\n#endif\n')
    dirname = os.path.join(get_build_dir(), 'include')
    path = os.path.join(dirname, 'sysmap.h')
    with open(path, 'w') as f:
        f.writelines(lines)

def generate():
    home = get_home_dir()
    build = get_build_dir()
    if os.path.exists(os.path.join(home, 'lib')):
        dirs = ['src', 'include', 'lib']
    else:
        dirs = ['src', 'include'] 
    for i in dirs:
        dir_dest = os.path.join(build, i)
        dir_src = os.path.join(home, i)
        copy_dir(dir_src, dir_dest)
    gen_sysmap()

def chk_conf():
    name = platform.system()
    if name != PLAT[0]:
        raise Exception('Error: %s is not supported' % name)

    if not INFO.has_key('tools'):
        INFO['tools'] = {}

    if not INFO.has_key('version'):
        INFO['version'] = '0.0.0'

    if not INFO['name']:
        INFO['name'] = os.path.basename(get_home_dir())

def chk_tools():
    tools = ['aclocal', 'autoconf', 'autoheader', 'automake', 'autoreconf']
    for i in tools:
        if i not in DEPS:
            DEPS.append(i)
    for i in DEPS:
        if not check_output(['which', i]):
            raise Exception('cannot find %s' %i)
    version = None
    ret = check_output('autoconf -V | head -n1', shell=True)
    if ret:
        version = ret.split(' ')[-1]
    if not version:
        raise Exception('cannot get the version of autoconf')
    INFO['tools']['autoconf'] = {'version':version}

def do_get_source_files(dirname, files, recursive):
    dir_src = os.path.join(get_build_dir(), dirname)
    for i in os.listdir(dir_src):
        path = os.path.join(dir_src, i)
        if not os.path.isdir(path):
            if i.endswith('.c'):
                files.append(os.path.join(dirname, i))
        elif not i.startswith('.'):
            if recursive:
                do_get_source_files(os.path.join(dirname, i), files, recursive)

def get_source_files(dirname, recursive=False):
    files = []
    do_get_source_files(dirname, files, recursive)
    return files

def get_source_list(head, files):
    cnt = 0
    for i in files:
        if cnt % 8 == 7:
            head += '\\\n\t'
        head += '%s ' % i
        cnt += 1
    return head + '\n'

def chk_proj():
    global DEFS
    lines = []
    lines.append('AC_PREREQ([%s])\n' % INFO['tools']['autoconf']['version'])
    lines.append('AC_INIT([%s], [%s])\n' % (INFO['name'], INFO['version']))
    lines.append('AC_CONFIG_SRCDIR([config.h.in])\n')
    lines.append('AC_CONFIG_HEADERS([config.h])\n')
    lines.append('AC_CONFIG_MACRO_DIR([m4])\n')
    lines.append('LT_INIT\n')
    lines.append('AC_PROG_CC\n')
    if LIBS:
        lines.append('AC_CHECK_LIB([%s])\n' % ' '.join(LIBS))
    lines.append('AM_INIT_AUTOMAKE([foreign subdir-objects -Werror])\n')
    lines.append('AC_OUTPUT([Makefile])\n')

    dirname = get_build_dir()
    path = os.path.join(dirname, 'configure.ac')
    with open(path, 'w') as f:
        f.writelines(lines)
    libs = []
    dir_src = os.path.join(dirname, 'src')
    for i in os.listdir(dir_src):
        if not i.startswith('.'):
            path = os.path.join(dir_src, i)
            if os.path.isdir(path):
                libs.append(i)
    if 'ext' in libs:
        raise Exception('Error: lib ext is reserved')
    lines = []
    lines.append('ACLOCAL_AMFLAGS = -I m4\n')
    lines.append('LDFLAGS = -L/usr/local/lib\n')
    lines.append('ARFLAGS = crUu\n')
    if LIBS:
        lines.append('LIBS = %s\n' % ' '.join(["-l%s" % i for i in LIBS]))
    if not GDB:
        cflags = 'AM_CFLAGS = -I/usr/local/include -I./include -I./src -Wno-unused-result'
    else:
        cflags = 'AM_CFLAGS = -ggdb -I/usr/local/include -I./include -I./src -Wno-unused-result'
    if INCL:
        cflags += ' %s' % ' '.join(['-I%s' % i for i in INCL])
    if libs:
        cflags += ' %s' % ' '.join(['-I./src/%s' % i for i in libs])
    if os.path.exists(os.path.join(dirname, 'lib')):
        ext = True
        cflags += ' -I./lib'
    else:
        ext = False
    if DEFS:
        cflags += ' %s' % ' '.join(['-D%s' % i for i in DEFS])
    lines.append(cflags + '\n\n')
    lines.append('bin_PROGRAMS = %s\n' % INFO['name'])

    sources = '%s_SOURCES = ' % INFO['name']
    files = get_source_files('src')
    lines.append(get_source_list(sources, files))

    lib_list = ' '.join(['lib%s.a' % i for i in libs])
    if not ext:
        lines.append('%s_LDADD = %s\n\n' % (INFO['name'], lib_list))
    else:
        lines.append('%s_LDADD = libext.a %s\n\n' % (INFO['name'], lib_list))
    lines.append('AUTOMAKE_OPTIONS = foreign\n')
    if not ext:
        lines.append('noinst_LIBRARIES = %s\n\n' % lib_list)
    else:
        lines.append('noinst_LIBRARIES = libext.a %s\n\n' % lib_list)

    for name in libs:
        sources = 'lib%s_a_SOURCES = ' % name
        path = os.path.join('src', name)
        files = get_source_files(path, recursive=True)
        lines.append(get_source_list(sources, files))
        lines.append('\n')

    if ext:
        sources = 'libext_a_SOURCES = '
        files = get_source_files('lib')
        lines.append(get_source_list(sources, files))
        lines.append('\n')

    path = os.path.join(dirname, 'Makefile.am')
    with open(path, 'w') as f:
        f.writelines(lines)

    path = os.path.join(dirname, 'm4')
    if not os.path.isdir(path):
        os.makedirs(path, 0o755)

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
            t = type(PARAMS[key]) 
            if t == int:
                PARAMS[key] = int(val)
            elif t == bool:
                PARAMS[key] = bool(int(val))
            else:
                PARAMS[key] = val
            print("%s: %s" % (str(key), str(PARAMS[key])))

def chk_params():
    read_params()
    for i in PARAMS:
        res = PARAMS[i]
        if None == res:
            raise Exception('Error: %s is not set' % i)
        val = ''
        t = type(PARAMS[i])
        if t == bool:
            val = i.upper()
            if not PARAMS[i] or val in DEFS:
                continue
        elif t == str:
            val = '%s=\'"%s"\'' % (i.upper(), PARAMS[i])
        elif t == int:
            val = '%s=%d' % (i.upper(), res)
        else:
            raise Exception('Error: invalid type of %s' % i)
        DEFS.append(val)

def configure():
    chk_params()
    chk_conf()
    chk_tools()
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
    path = get_build_dir()
    invoke('aclocal', path, quiet=True)
    invoke('autoconf', path, quiet=True)
    invoke('autoheader', path, quiet=True)
    invoke('touch NEWS README AUTHORS ChangeLog', path)
    invoke('automake --add-missing', path, quiet=True, ignore=True)
    invoke('autoreconf -if', path, quiet=True)
    invoke('./configure', path, quiet=True)
    invoke('make', path)

if __name__ == '__main__':
    generate()
    configure()
    build()
