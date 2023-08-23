DEPS = []
INCL = ['src/vres/log']
PLAT = ['Linux', '5.4.161', '64bit', 'x86']
INFO = {'name': 'klnk', 'version': '0.1'}
LIBS = ['pthread', 'rt', 'fuse3', 'hiredis']
DEFS = ['_GNU_SOURCE', '_FILE_OFFSET_BITS=64']
SYSCALLS = ['shm_rdprotect', 'shm_wrprotect', 'shm_present']
PARAMS = {
    'manager_type' : 0,
    'veth'         : True,
    'node'         : True,
    'debug'        : True,
    'error'        : True,
    'warning'      : True,
    'unshare'      : True,
    'log2file'     : True,
    'evaluate'     : True,
    'trace_req'    : True,
    'mds_port'     : 0,
    'klnk_port'    : 0,
    'ifname'       : '',
    'path_log'     : '',
    'path_fs'      : '',
    'path_dump'    : '',
    'path_conf'    : '',
    'path_cache'   : '',
    'path_kernel'  : '/usr/src/%s-%s' % (PLAT[0].lower(), PLAT[1]),
}
