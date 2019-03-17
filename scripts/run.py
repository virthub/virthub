import os
import sys
import argparse
import subprocess

_name = subprocess.check_output('readlink -f %s' % sys.argv[0], shell=True)
_path = os.path.dirname(_name).decode("utf-8")
_dir = os.path.dirname(_path)
sys.path.append(_dir)
sys.path.append(os.path.join(_dir, 'conf'))

from lib import fs
from init import *
from lib.util import addr2name

def usage():
    print('usage: %s [addr] [-a|-d|-c]')
    print("addr: the address of the target environment")
    print("-a: attach to the target environment")
    print("-d: detach from the target environment")
    print("-c: create an environment")

def _clean(addr):
    name = addr2name(addr)
    path = fs.get_log_path()
    os.system("rm -f %s" % os.path.join(path, "klnk_%s.log" % name))
    os.system("rm -f %s" % os.path.join(path, "lbfs_%s.log" % name))

if __name__ == '__main__':
    if len(sys.argv) != 3:
        usage()
        sys.exit(-1)
    parser = argparse.ArgumentParser()
    parser.add_argument('-a', '--attach', action='store_true')
    parser.add_argument('-d', '--detach', action='store_true')
    parser.add_argument('-c', '--create', action='store_true')
    args = parser.parse_args(sys.argv[2:])
    addr = sys.argv[1]
    if args.attach:
        _clean(addr)
        attach(addr)
    elif args.detach:
        detach(addr)
    elif args.create:
        _clean(addr)
        create(addr)
    else:
        usage()
        sys.exit(-1)
