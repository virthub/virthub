IPC            = True
CKPT           = True
PROBE          = True
FS             = True

SHOW_ERROR     = True
SHOW_DEBUG     = True

FS_PORT        = 22001
MDS_PORT       = 23001
MONITOR_PORT   = 24001
DAEMON_PORT    = 16101
BRIDGE_PORT    = 21101    # bridge server port

IFNAME         = 'eth0'   # net adapter of host
BR_NAME        = 'lxcbr0' # container bridge
BRIDGE_SERVERS = []       # bridge servers

PATH_BIN      = '/vhub/bin'
PATH_RUN      = '/vhub/run'
PATH_VAR      = '/vhub/var'
PATH_MNT      = '/vhub/mnt'
PATH_CONF     = '/vhub/conf'
PATH_INIT     = '/vhub/init'
PATH_ROOT     = '/vhub/root/bin'
PATH_KLNK     = '/vhub/mnt/klnk'
PATH_LOG      = '/vhub/var/log'
PATH_LXC_LIB  = '/var/lib/lxc'
PATH_LXC_ROOT = '/var/cache/lxc/rootfs'

# File systems
MATEFS = False

# Tools
DUMP = False

# Modules
CKPT = False
KLNK = True
VMAP = False

DEPS   = []
PLAT   = ['Linux']
INFO   = {'name': 'virthub', 'version': '0.1'}
PKG    = ['lxc', 'n2n', 'curlftpfs', 'python-pip', 'debootstrap', 'redis-server']
PYLIB  = ['netifaces', 'pyftpdlib', 'hash_ring', 'redis', 'twisted', 'autobahn', 'wget', 'zerorpc']
MIRROR = 'http://archive.ubuntu.com/ubuntu/'
