import os
import shlex
import random
import shutil
import tempfile
from lib.log import log
from lib import edge, fs, ipc, ckpt
from commands import getstatusoutput, getoutput
from lib.util import ifaddr, killall, mkdirs, popen, rmdir, get_home, bind, umount
from defaults import PATH_LXC_LIB, PATH_LXC_ROOT, PATH_BIN, PATH_MNT, PATH_RUN, PATH_VAR, PROBE, FS, CKPT, BR_NAME

PRINT = True
SHOW_CMD = False
LXC_NAME = 'lxc'
AA_ALLOW_INCOMPLETE = False
AA_PROFILE_UNCONFINED = True

def _log(text):
    if PRINT:
        log('container: ' + text)

def _get_log_path(name):
    return os.path.join(PATH_VAR, 'log', '%s.%s.log' % (LXC_NAME, name))

def _get_path_run(name):
    return os.path.join(PATH_RUN, '%s.%s.pid' % (LXC_NAME, name))

def _check_hwaddr():
    return "00:16:3e:" + ":".join(['%02x' % random.randint(0, 255) for _ in range(3)])

def _check_cfg(name):
    dirname = os.path.join(PATH_LXC_LIB, name)
    mkdirs(dirname)
    cfg = []
    if PROBE:
        cfg.append('lxc.network.type = veth\n')
        cfg.append('lxc.network.link = %s\n' % BR_NAME)
        cfg.append('lxc.network.flags = up\n')
        cfg.append('lxc.network.hwaddr = %s\n' % _check_hwaddr())
    cfg.append('lxc.rootfs = %s\n' % os.path.join(dirname, 'rootfs'))
    cfg.append('lxc.mount = %s\n' % os.path.join(dirname, 'fstab'))
    cfg.append('lxc.utsname = %s\n' % name)
    cfg.append('lxc.devttydir = lxc\n')
    cfg.append('lxc.tty = 4\n')
    cfg.append('lxc.pts = 1024\n')
    cfg.append('lxc.arch = i686\n')
    cfg.append('lxc.cap.drop = sys_module mac_admin\n')
    if AA_ALLOW_INCOMPLETE:
        cfg.append('lxc.aa_allow_incomplete = 1\n')
    if AA_PROFILE_UNCONFINED:
        cfg.append('lxc.aa_profile = unconfined\n')
    #cfg.append('lxc.pivotdir = lxc_putold\n')
    cfg.append('lxc.cgroup.devices.deny = a\n')
    cfg.append('lxc.cgroup.devices.allow = c *:* m\n')
    cfg.append('lxc.cgroup.devices.allow = b *:* m\n')
    cfg.append('lxc.cgroup.devices.allow = c 1:3 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 1:5 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 5:1 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 5:0 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 1:9 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 1:8 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 136:* rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 5:2 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 254:0 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 10:229 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 10:200 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 1:7 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 10:228 rwm\n')
    cfg.append('lxc.cgroup.devices.allow = c 10:232 rwm\n')
    path = os.path.join(dirname, 'config')
    with open(path, 'w') as f:
        f.writelines(cfg)

def _check_fstab(name):
    fstab = []
    fstab.append('proc            proc         proc    nodev,noexec,nosuid 0 0\n')
    fstab.append('sysfs           sys          sysfs defaults  0 0\n')
    src = fs.get_mountpoint(name)
    dest = fs.get_binding(name)
    mkdirs(dest)
    fstab.append('%s      %s       none   bind  0 0\n' % (src, dest))
    src = ipc.get_mountpoint(name)
    dest = ipc.get_binding(name)
    mkdirs(dest)
    fstab.append('%s      %s       none   bind  0 0\n' % (src, dest))
    if FS:
        src = fs.get_mountpoint(name, fs=True)
        dest = os.path.join(fs.get_mnt(name), fs.get_name())
        mkdirs(dest)
        fstab.append('%s      %s       none   bind  0 0\n' % (src, dest))
    if CKPT:
        src = ckpt.get_mountpoint(name)
        dest = os.path.join(fs.get_mnt(name), ckpt.get_name())
        mkdirs(dest)
        fstab.append('%s      %s       none   bind  0 0\n' % (src, dest))
    path = os.path.join(PATH_LXC_LIB, name, 'fstab')
    with open(path, 'w') as f:
        f.writelines(fstab)

def _check_root(name):
    path = os.path.join(PATH_LXC_LIB, name, 'rootfs')
    os.system('cp -ax --reflink=always %s/. %s' % (PATH_LXC_ROOT, path))

def _check_probe(name):
    interfaces = []
    interfaces.append('auto lo\n')
    interfaces.append('iface lo inet loopback\n')
    interfaces.append('auto eth0\n')
    interfaces.append('iface eth0 inet dhcp\n')
    path = os.path.join(PATH_LXC_LIB, name, 'rootfs', 'etc', 'network', 'interfaces')
    with open(path, 'w') as f:
        f.writelines(interfaces)

def _start_container(name):
    pid = ipc.get_pid(name)
    if not pid:
        log('failed to start container')
        raise Exception('failed to start container')
    cfg = os.path.join(PATH_LXC_LIB, name, 'config')
    cmd = ['lxc-start', '-F', '-P', PATH_LXC_LIB, '-p', _get_path_run(name), '-n', name, '-f', cfg, '--share-ipc', str(pid), '-o', _get_log_path(name), '-c', '/dev/null']
    if SHOW_CMD:
        _log('%s' % ' '.join(cmd))
    popen(cmd)
    os.system('lxc-wait -n %s -s "RUNNING"' % name)
    os.system('lxc-device add -n %s /dev/ckpt' % name)

def _stop(name):
    os.system('lxc-stop -n %s 2>/dev/null' % name)

def _remove(name):
    path = os.path.join(PATH_LXC_LIB, name, 'rootfs')
    if os.path.exists(path):
        os.system('btrfs subvolume delete %s 2>/dev/null' % path)
    path = os.path.join(PATH_LXC_LIB, name)
    if os.path.exists(path):
        rmdir(path)

def _release(name):
    path = os.path.join(PATH_LXC_LIB, name)
    if os.path.isdir(path):
        try:
            _stop(name)
            fs.stop(name)
            ipc.stop(name)
            edge.stop(name)
            _remove(name)
        except:
            pass

def clean(name):
    _release(name)

def _start(name):
    _log('checking cfg')
    _check_cfg(name)
    _log('checking fstab')
    _check_fstab(name)
    _log('checking root')
    _check_root(name)
    if PROBE:
        _log('checking probe')
        _check_probe(name)
    _start_container(name)

def start(name):
    _log('start, name=%s' % name)
    _start(name)

def create(name):
    _start(name)

def stop(name):
    _release(name)

def remove(name):
    _release(name)
