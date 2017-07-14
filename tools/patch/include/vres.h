#ifndef _VRES_H
#define _VRES_H

enum vres_class {
	VRES_CLS_MSG,
	VRES_CLS_SEM,
	VRES_CLS_SHM,
	VRES_CLS_TSK,
	VRES_NR_CLASSES,
};

enum vres_operation {
	VRES_OP_UNUSED0,
	VRES_OP_MSGSND,
	VRES_OP_MSGRCV,
	VRES_OP_MSGCTL,
	VRES_OP_SEMOP,
	VRES_OP_SEMCTL,
	VRES_OP_SEMEXIT,
	VRES_OP_SHMFAULT,
	VRES_OP_SHMCTL,
	VRES_OP_TSKCTL,
	VRES_OP_UNUSED1,
	VRES_OP_TSKGET,
	VRES_OP_MSGGET,
	VRES_OP_SEMGET,
	VRES_OP_SHMGET,
	VRES_OP_SHMPUT,
	VRES_OP_TSKPUT,
	VRES_OP_MIGRATE,
	VRES_OP_DUMP,
	VRES_OP_RESTORE,
	VRES_OP_RELEASE,
	VRES_OP_PROBE,
	VRES_OP_UNUSED2,
	VRES_OP_JOIN,
	VRES_OP_LEAVE,
	VRES_OP_SYNC,
	VRES_OP_REPLY,
	VRES_OP_CANCEL,
	VRES_OP_UNUSED3,
	VRES_NR_OPERATIONS,
};

#define EOK				900
#define ENOOWNER			901
#define ERMID				902
#define EMIGRATE			903

#define VRES_RDONLY			0x00000001
#define VRES_RDWR			0x00000002	

#define VRES_POS_OP			0
#define VRES_POS_ID			1
#define VRES_POS_KEY		1
#define VRES_POS_VAL1		2
#define VRES_POS_CLS		2
#define VRES_POS_VAL2		3
#define VRES_POS_INDEX		3

#define VRES_PATH_MAX		128
#define VRES_ERRNO_MAX		1000
#define VRES_INDEX_MAX		((1 << 30) - 1)

typedef key_t vres_key_t;
typedef int32_t vres_id_t;
typedef int32_t vres_val_t;
typedef int32_t vres_entry_t;
typedef int32_t vres_index_t;
typedef uint32_t vres_cls_t;
typedef uint32_t vres_flg_t;
typedef uint32_t vres_op_t;

static inline void vres_set_entry(vres_entry_t *entry, vres_op_t op, vres_id_t id, vres_val_t val1, vres_val_t val2) 
{
	entry[VRES_POS_OP] = (vres_entry_t)op;
	entry[VRES_POS_ID] = (vres_entry_t)id; 
	entry[VRES_POS_VAL1] = (vres_entry_t)val1;
	entry[VRES_POS_VAL2] = (vres_entry_t)val2; 
}

typedef struct vres {
	vres_cls_t cls;
	vres_key_t key;
	vres_entry_t entry[4];
} vres_t;

typedef struct vres_cancel_arg {
	vres_op_t op;
	vres_index_t index;
} vres_cancel_arg_t;

typedef struct vres_msgrcv_result {
	long retval;
} vres_msgrcv_result_t;

typedef struct vres_msgrcv_arg {
	long msgtyp;
	size_t msgsz;
	int msgflg;
} vres_msgrcv_arg_t;

typedef struct vres_msgctl_arg {
	int cmd;
} vres_msgctl_arg_t;

typedef struct vres_msgctl_result {
	long retval;
} vres_msgctl_result_t;

typedef struct vres_semop_arg {
	struct timespec timeout;
	short nsops;
	struct sembuf sops[0];
} vres_semop_arg_t;

typedef struct vres_semop_result {
	long retval;
} vres_semop_result_t;

typedef struct vres_semctl_arg {
	int semnum;
	int cmd;
} vres_semctl_arg_t;

typedef struct vres_semctl_result {
	long retval;
} vres_semctl_result_t;

typedef struct vres_msgsnd_arg {
	long msgtyp;
	size_t msgsz;
	int msgflg;
} vres_msgsnd_arg_t;

typedef struct vres_msgsnd_result {
	long retval;
} vres_msgsnd_result_t;	

typedef struct vres_shmfault_result {
	long retval;
	char buf[PAGE_SIZE];
} vres_shmfault_result_t;

typedef struct vres_shmctl_arg {
	int cmd;
} vres_shmctl_arg_t;

typedef struct vres_shmctl_result {
	long retval;
} vres_shmctl_result_t;

typedef struct vres_mig_arg {
	char path[VRES_PATH_MAX];
} vres_mig_arg_t;

#endif
