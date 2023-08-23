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
    for i in ['src', 'include']:
        dir_dest = os.path.join(build, i)
        dir_src = os.path.join(home, i)
        copy_dir(dir_src, dir_dest)

def chk_conf():
    name = platform.system()
    if name not in PLAT:
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

    lines = []
    lines.append('ACLOCAL_AMFLAGS = -I m4\n')
    lines.append('LDFLAGS = -L/usr/local/lib\n')
    lines.append('ARFLAGS = crUu\n')
    if LIBS:
        lines.append('LIBS = %s\n' % ' '.join(["-l%s" % i for i in LIBS]))
    cflags = 'AM_CFLAGS = -I/usr/local/include -I./include -I./src -Wno-unused-result'
    if INCL:
        cflags += ' %s' % ' '.join(['-I%s' % i for i in INCL])
    if libs:
        cflags += ' %s' % ' '.join(['-I./src/%s' % i for i in libs])
    if DEFS:
        cflags += ' %s' % ' '.join(['-D%s' % i for i in DEFS])
    lines.append(cflags + '\n\n')
    lines.append('bin_PROGRAMS = %s\n' % INFO['name'])

    sources = '%s_SOURCES = ' % INFO['name']
    files = get_source_files('src')
    lines.append(get_source_list(sources, files))

    lib_list = ' '.join(['lib%s.a' % i for i in libs])
    lines.append('%s_LDADD = %s\n\n' % (INFO['name'], lib_list))
    lines.append('AUTOMAKE_OPTIONS = foreign\n')
    lines.append('noinst_LIBRARIES = %s\n\n' % lib_list)

    for name in libs:
        sources = 'lib%s_a_SOURCES = ' % name
        path = os.path.join('src', name)
        files = get_source_files(path, recursive=True)
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
