# SERVICES ############################
IPC = True
CKPT = True
MIFS = True
DEBUG = True
PROBE = True
FRONTEND = False
#######################################

# LOGGING #############################
SHOW_ERROR = True
SHOW_DEBUG = True
#######################################

# APPARMOR ############################
AA_ALLOW_INCOMPLETE = False
AA_PROFILE_UNCONFINED = True
#######################################

# NETWORK #############################
BR_NAME = 'lxcbr0'
PROBE_PORT = 20000
#######################################

# NET ADAPTER #########################
IFNAME = 'eth0'
#######################################

# EXTERNAL SERVERS ####################
FS_PORT = 22001
METADATA_PORT = 23001
MONITOR_PORT = 24001
NOTIFIER_PORT = 27001
#######################################

# INTERNAL SERVERS ####################
DAEMON_PORT = 16101  # Virtdev Daemon #
BRIDGE_PORT = 21101  # Virtdev Bridge #
BRIDGE_SERVERS = []
#######################################
