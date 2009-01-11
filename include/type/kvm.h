/*
 * kvm.h
 *
 * Key-Value Map
 *
 * Copyright 2002, 2006 by Anthony Howe.  All rights reserved.
 */

#ifndef __com_snert_lib_type_kvm_h__
#define __com_snert_lib_type_kvm_h__	1

#ifdef __cplusplus
extern "C" {
#endif

#include <com/snert/lib/version.h>

#include <sys/types.h>

#include <com/snert/lib/berkeley_db.h>
#include <com/snert/lib/sys/pthread.h>

#define KVM_DELIM			'!'
#define KVM_DELIM_S			"!"

#define KVM_PORT			7953
#define KVM_PORT_S			"7953"

#define KVM_MODE_READ_ONLY		1
#define KVM_MODE_DB_BTREE		2
#define KVM_MODE_DB_STAT		4
#define KVM_MODE_KEY_HAS_NUL		8

#define KVM_OK				0
#define KVM_ERROR			(-1)
#define KVM_NOT_FOUND			(-2)
#define KVM_NOT_IMPLEMETED		(-3)

typedef struct {
	unsigned long size;
	unsigned char *data;
} kvm_data;

typedef struct kvm {
	void (*close)(struct kvm *self);
	const char *(*filepath)(struct kvm *self);
	int (*fetch)(struct kvm *self, kvm_data *key, kvm_data *value);
	int (*get)(struct kvm *self, kvm_data *key, kvm_data *value);
	int (*put)(struct kvm *self, kvm_data *key, kvm_data *value);
	int (*remove)(struct kvm *self, kvm_data *key);
	int (*truncate)(struct kvm *self);
	int (*begin)(struct kvm *self);
	int (*commit)(struct kvm *self);
	int (*rollback)(struct kvm *self);
#ifdef NOT_FINISHED
/* Object locking becomes an issue with first/next. */
	int (*first)(struct kvm *self);
	int (*next)(struct kvm *self, kvm_data *key, kvm_data *value);
#else
	int (*walk)(struct kvm *self, int (*function)(kvm_data *, kvm_data *, void *), void *data);
#endif
	void (*sync)(struct kvm *self);
#ifdef HAVE_PTHREAD_MUTEX_T
	pthread_mutex_t _mutex;
#endif
	char *_location;
	char *_table;
	void *_kvm;
	int _mode;
} kvm;

/**
 * @param table_name
 *
 * @param map_location
 *	A pointer to a C string specifying the type and location of
 *	the key-value map. The following are currently supported:
 *
 *		NULL				(assumes hash!)
 *		hash!
 *		file!/path/map.txt
 *		/path/map.db			(historical)
 *		db!/path/map.db
 *		db!btree!/path/map.db
 *		multicast!group,port!map
 *		socketmap!host,port
 *		socketmap!/path/local/socket
 *		sql!/path/database
 *
 * @param mode
 *	A bit mask of KVM_MODE_ flags.
 *
 * @return
 *	A pointer to a kvm structure on success, otherwise NULL. Its
 *	the caller's responsiblity to call the kvm's close method
 *	once done.
 */
extern kvm *kvmOpen(const char *table_name, const char *map_name, int mode);

extern void kvmDebug(int flag);

#ifdef NOT_FINISHED
/* Object locking becomes an issue with first/next. */
extern int kvmLock(kvm *self);
extern int kvmUnlock(kvm *self);
#endif

extern void kvmAtForkPrepare(kvm *self);
extern void kvmAtForkParent(kvm *self);
extern void kvmAtForkChild(kvm *self);

#ifdef  __cplusplus
}
#endif

#endif /* __com_snert_lib_type_kvm_h__ */

