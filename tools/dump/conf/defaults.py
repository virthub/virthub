DEPS = []
INCL = []
PLAT = ['Linux']
INFO = {'name': '', 'version': '0.0.1'}
LIBS = []
DEFS = ['ERROR']
ARGS = {
    'debug': {'type': 'bool'},
    'clone': {'type': 'bool'},
    'log2file': {'type': 'bool'},
    'path_log': {'type': 'str', 'default': '/vhub/var/log'},
    'path_klnk': {'type': 'str', 'default': '/vhub/mnt/klnk'},
    'path_cache': {'type': 'str', 'default': '/vhub/var/cache'}
}
