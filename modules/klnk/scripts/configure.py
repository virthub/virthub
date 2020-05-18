DEPS = []
PLAT = ['Linux']
INCL = ['src/vres/log']
INFO = {'name': '', 'version': '0.0.1'}
LIBS = ['pthread', 'rt', 'fuse', 'hiredis']
DEFS = ['_GNU_SOURCE', '_FILE_OFFSET_BITS=64']
ARGS = {
    'veth': {'type': 'bool'},
    'node': {'type': 'bool'},
    'debug': {'type': 'bool'},
    'error': {'type': 'bool'},
    'warning': {'type': 'bool'},
    'unshare': {'type': 'bool'},
    'log2file': {'type': 'bool'},
    'evaluate': {'type': 'bool'},
    'trace_proc': {'type': 'bool'},
    'ifname': {'type': 'str', 'default': 'eth0'},
    'manager_type': {'type': 'int', 'default': '0'},
    'mds_port': {'type': 'int', 'default': '23001'},
    'klnk_port': {'type': 'int', 'default': '12000'},
    'path_log': {'type': 'str', 'default': '/vhub/var/log'},
    'path_lbfs': {'type': 'str', 'default': '/vhub/mnt/lbfs'},
    'path_dump': {'type': 'str', 'default': '/vhub/init/dump'},
    'path_conf': {'type': 'str', 'default': '/vhub/conf/klnk'},
    'path_cache': {'type': 'str', 'default': '/vhub/var/cache'}
}
