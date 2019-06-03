DEPS = []
INCL = []
PLAT = ['Linux']
INFO = {'name': '', 'version': '0.0.1'}
LIBS = ['fuse']
DEFS = ['ERROR', '_FILE_OFFSET_BITS=64']
ARGS = {
    'debug': {'type': 'bool'},
    'log2file': {'type': 'bool'},
    'path_log': {'type': 'str', 'default': '/vhub/var/log'}
}
