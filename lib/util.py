import os
import sys
import time
import psutil
import socket
import struct
import shutil
from signal import SIGKILL
from defaults import IFNAME
from subprocess import Popen
from commands import getoutput
from netifaces import ifaddresses, AF_INET

DIR_MODE = 0o755
HEARTBEAT_INTERVAL = 32 # seconds
HEARTBEAT_SAFE = HEARTBEAT_INTERVAL / 2 # seconds

RETRY_MAX = 5
WAIT_TIME = 0.1 # seconds
RETRY_INTERVAL = 0.2 # seconds

DEVNULL = open(os.devnull, 'wb')

_ifaddr = None

def zmqaddr(addr, port):
    return 'tcp://%s:%d' % (str(addr), int(port))

def ifaddr(ifname=IFNAME):
    global _ifaddr
    if ifname == IFNAME and _ifaddr:
        return _ifaddr
    else:
        iface = ifaddresses(ifname)[AF_INET][0]
        addr = iface['addr']
        if ifname == IFNAME:
            _ifaddr = addr
        return addr

def name2addr(name):
    return socket.inet_ntoa(struct.pack('I', int(name, 16)))

def addr2name(addr):
    return '%08x' % struct.unpack('I', socket.inet_aton(addr))[0]

def kill(pid):
    if pid:
        os.system("kill %s 2>/dev/null" % str(pid))

def killall(name):
    if name:
        os.system('killall %s 2>/dev/null' % name)

def kill_by_file(name, path):
    if os.path.exists(path):
        with open(path, 'r') as f:
            pid = int(f.readlines()[0].strip())
        if get_tsk_name_by_pid(pid) == name:
            kill(pid)
            os.remove(path)

def close_port(port):
    pid = getoutput('lsof -i:%d -Fp | cut -c2-' % port)
    kill(pid)

def umount(path):
    os.system('umount -lf %s 2>/dev/null' % path)
    time.sleep(WAIT_TIME)

def bind(src, dest):
    os.system('mount -o bind %s %s' % (src, dest))

def get_tsk_name_by_pid(pid):
    if pid:
        return getoutput("ps -p %s -o comm=" % str(pid))
    else:
        return ""

def get_home():
    return getoutput('readlink -f ~')

def save_pid(path, pid):
    with open(path, 'w') as f:
        f.write(str(pid))

def popen(cmd):
    return Popen(cmd, stdout=DEVNULL, stderr=DEVNULL).pid

def chkpid(pid):
    for _ in range(RETRY_MAX):
        try:
            if psutil.Process(pid).status() == psutil.STATUS_ZOMBIE:
                return False
            else:
                return True
        except:
            time.sleep(RETRY_INTERVAL)

def chkaddr(name):
    for _ in range(RETRY_MAX):
        try:
            ret = ifaddr(name)
            if ret:
                return ret
            else:
                time.sleep(RETRY_INTERVAL)
        except:
            time.sleep(RETRY_INTERVAL)

def mkdirs(dirname):
    if dirname:
        os.system("mkdir -p %s" % dirname)

def cpdir(src, dest):
    os.system('cp %s/* %s' % (src, dest))

def rmdir(dirname):
    if dirname:
        shutil.rmtree(dirname, ignore_errors=True)
