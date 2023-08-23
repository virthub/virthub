PLAT = ['Linux', '5.4.161']
INFO = {'name':'ckpt', 'version': '0.0.1'}
DEFS = ['ERROR']
PARAMS = {
    'debug': True,
    'ckpt_map': True,
    'show_map': True,
    'show_dump': True,
    'show_restore': True,
    'path_map': '/vhub/mnt/vmap'
}
SYSCALLS = [
    'arch_pick_mmap_layout',
    'arch_setup_additional_pages',
    'copy_siginfo_to_user',
    'do_mmap',
    'do_munmap',
    'do_sigaction',
    'find_task_by_vpid',
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
