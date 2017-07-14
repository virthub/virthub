DEPS = []
INCL = ['src/res/log']
PLAT = ['Linux']
INFO = {'name': '', 'version': '0.0.1'}
LIBS = ['pthread', 'rt', 'fuse', 'hiredis']
DEFS = ['ERROR', '_GNU_SOURCE', '_FILE_OFFSET_BITS=64']
ARGS = {
    'node': {'type': 'bool'},
    'debug': {'type': 'bool'},
    'log2file': {'type': 'bool'},
    'evaluate': {'type': 'bool'},
    'container': {'type': 'bool'},
    'show_mig': {'type': 'bool'},
    'show_shm': {'type': 'bool'},
    'show_sem': {'type': 'bool'},
    'show_msg': {'type': 'bool'},
    'show_tsk': {'type': 'bool'},
    'show_tmp': {'type': 'bool'},
    'show_ipc': {'type': 'bool'},
    'show_node': {'type': 'bool'},
    'show_more': {'type': 'bool'},
    'show_dump': {'type': 'bool'},
    'show_klnk': {'type': 'bool'},
    'show_sync': {'type': 'bool'},
    'show_ckpt': {'type': 'bool'},
    'show_proc': {'type': 'bool'},
    'show_page': {'type': 'bool'},
    'show_line': {'type': 'bool'},
    'show_lock': {'type': 'bool'},
    'show_redo': {'type': 'bool'},
    'show_prio': {'type': 'bool'},
    'show_event': {'type': 'bool'},
    'show_member': {'type': 'bool'},
    'show_record': {'type': 'bool'},
    'show_rwlock': {'type': 'bool'},
    'show_restore': {'type': 'bool'},
    'show_request': {'type': 'bool'},
    'show_barrier': {'type': 'bool'},
    'ifname': {'type': 'str', 'default': 'eth0'},
    'metadata_port': {'type': 'int', 'default': '23001'},
    'klnk_port': {'type': 'int', 'default': '12000'},
    'path_log': {'type': 'str', 'default': '/var/log'},
    'path_mifs': {'type': 'str', 'default': '/mnt/mifs'},
    'path_dump': {'type': 'str', 'default': '/usr/bin/dump'},
    'path_conf': {'type': 'str', 'default': '/etc/default/klnk'},
    'manager': {'type': 'int', 'default': '0', 'map': {'0': 'NOMANAGER', '1': 'STATIC_MANAGER', '2': 'DYNAMIC_MANAGER'}}
}
