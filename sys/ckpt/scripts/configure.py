PLAT = ['Linux']
INFO = {'name':'', 'version': '0.0.1'}
DEFS = ['ERROR']
ARGS = {
    'debug': {'type': 'bool'},
    'ckpt_map': {'type': 'bool'},
    'show_map': {'type': 'bool'},
    'show_dump': {'type': 'bool'},
    'show_restore': {'type': 'bool'},
    'path_map': {'type': 'str', 'default': '/mnt/vmap'}
}
SYSCALLS = [
    'arch_pick_mmap_layout',
    'arch_setup_additional_pages',
    'copy_siginfo_to_user',
    'do_mmap_pgoff',
    'do_munmap',
    'do_sigaction',
    'find_task_by_vpid',
    'flush_tlb_page',
    'group_send_sig_info',
    'groups_search',
    'handle_mm_fault',
    'set_dumpable',
    'set_fs_pwd',
    'set_fs_root',
    'suid_dumpable',
    'sys_lseek',
    'sys_mprotect',
    'sys_mremap',
    'sys_setgroups',
    'sys_setresgid',
    'sys_setresuid',
    'sys_prctl',
    'tasklist_lock',
    '__mm_populate',
]
SYSTEM_MAP = "/boot/System.map"
