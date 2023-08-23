import os
import sys
import argparse
import subprocess

_name = subprocess.check_output('readlink -f %s' % sys.argv[0], shell=True)
_path = os.path.dirname(_name).decode("utf-8")
_dir = os.path.dirname(_path)
sys.path.append(_dir)
sys.path.append(os.path.join(_dir, 'conf'))

from init import *
from conf.env import *
from lib.util import addr2name

def usage():
    print('usage: %s [-c|-a|-d] [ip address]')
    print("-c: create an environment")
    print("-a: attach to the target environment")
    print("-d: detach from the target environment")
    print("addr: the address of the target environment")

def _is_valid_addr(addr):
    fields = addr.split('.')
    if len(fields) != 4:
        usage()
        sys.exit(-1)
    for i in fields:
        val = int(i)
        if val < 0 or val > 255:
            usage()
            sys.exit(-1)

def _clean(addr):
    name = addr2name(addr)
    os.system("rm -f %s" % os.path.join(PATH_LOG, "klnk_%s.log" % name))
    os.system("rm -f %s" % os.path.join(PATH_LOG, "fs_%s.log" % name))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        usage()
        sys.exit(-1)
    parser = argparse.ArgumentParser()
    parser.add_argument('-a', '--attach', default='')
    parser.add_argument('-d', '--detach', default='')
    parser.add_argument('-c', '--create', default='')
    args = parser.parse_args(sys.argv[1:])
    if args.attach:
        _is_valid_addr(args.attach)
        _clean(args.attach)
        attach(args.attach)
    elif args.detach:
        _is_valid_addr(args.detach)
        detach(args.detach)
    elif args.create:
        _is_valid_addr(args.create)
        _clean(args.create)
        create(args.create)
    else:
        usage()
        sys.exit(-1)
