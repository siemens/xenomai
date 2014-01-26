/**
 * @file
 * Real-Time Driver Model for Xenomai, device management
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * @note Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*!
 * @addtogroup driverapi
 * @{
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <cobalt/kernel/apc.h>
#include "rtdm/internal.h"
#include <trace/events/cobalt-rtdm.h>

#define SET_DEFAULT_OP(device, operation)				\
	(device).operation##_rt  = (void *)rtdm_no_support;		\
	(device).operation##_nrt = (void *)rtdm_no_support

#define SET_DEFAULT_OP_IF_NULL(device, operation)			\
	if (!(device).operation##_rt)					\
		(device).operation##_rt = (void *)rtdm_no_support;	\
	if (!(device).operation##_nrt)					\
		(device).operation##_nrt = (void *)rtdm_no_support

#define ANY_HANDLER(device, operation)					\
	((device).operation##_rt || (device).operation##_nrt)

unsigned int devname_hashtab_size = DEF_DEVNAME_HASHTAB_SIZE;
module_param(devname_hashtab_size, uint, 0400);
MODULE_PARM_DESC(devname_hashtab_size,
		 "Size of hash table for named devices (must be power of 2)");

struct list_head *rtdm_named_devices;	/* hash table */
static int name_hashkey_mask;
struct rb_root rtdm_protocol_devices;

int rtdm_apc;
EXPORT_SYMBOL_GPL(rtdm_apc);

struct semaphore nrt_dev_lock;
DEFINE_XNLOCK(rt_dev_lock);

int rtdm_initialised = 0;

int rtdm_no_support(void)
{
	return -ENOSYS;
}

int rtdm_select_bind_no_support(struct rtdm_dev_context *context,
				struct xnselector *selector,
				unsigned type,
				unsigned index)
{
	return -EBADF;
}

static inline int get_name_hash(const char *str, int limit, int hashkey_mask)
{
	int hash = 0;

	while (*str != 0) {
		hash += *str++;
		if (--limit == 0)
			break;
	}
	return hash & hashkey_mask;
}

static inline unsigned long long get_proto_id(int pf, int type)
{
	unsigned long long llpf = (unsigned)pf;
	return (llpf << 32) | (unsigned)type;
}

static inline void rtdm_reference_device(struct rtdm_device *device)
{
	atomic_inc(&device->reserved.refcount);
}

struct rtdm_device *get_named_device(const char *name)
{
	struct list_head *entry;
	struct rtdm_device *device;
	int hashkey;
	spl_t s;

	hashkey = get_name_hash(name, RTDM_MAX_DEVNAME_LEN, name_hashkey_mask);

	xnlock_get_irqsave(&rt_dev_lock, s);

	list_for_each(entry, &rtdm_named_devices[hashkey]) {
		device = list_entry(entry, struct rtdm_device, reserved.entry);

		if (strcmp(name, device->device_name) == 0) {
			rtdm_reference_device(device);

			xnlock_put_irqrestore(&rt_dev_lock, s);

			return device;
		}
	}

	xnlock_put_irqrestore(&rt_dev_lock, s);

	return NULL;
}

struct rtdm_device *get_protocol_device(int protocol_family, int socket_type)
{
	struct rtdm_device *device;
	unsigned long long id;
	struct xnid *xnid;
	spl_t s;

	id = get_proto_id(protocol_family, socket_type);

	xnlock_get_irqsave(&rt_dev_lock, s);

	xnid = xnid_fetch(&rtdm_protocol_devices, id);
	if (xnid) {
		device = container_of(xnid, struct rtdm_device, reserved.id);

		rtdm_reference_device(device);
	} else
		device = NULL;

	xnlock_put_irqrestore(&rt_dev_lock, s);

	return device;
}

/*!
 * @ingroup driverapi
 * @defgroup devregister Device Registration Services
 * @{
 */

/**
 * @brief Register a RTDM device
 *
 * @param[in] device Pointer to structure describing the new device.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if the device structure contains invalid entries.
 * Check kernel log in this case.
 *
 * - -ENOMEM is returned if the context for an exclusive device cannot be
 * allocated.
 *
 * - -EEXIST is returned if the specified device name of protocol ID is
 * already in use.
 *
 * - -EAGAIN is returned if some /proc entry cannot be created.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 *
 * Rescheduling: never.
 */
int rtdm_dev_register(struct rtdm_device *device)
{
	unsigned long long id;
	int hashkey;
	spl_t s;
	struct list_head *entry;
	struct rtdm_device *existing_dev;
	int ret;

	/* Catch unsuccessful initialisation */
	if (!rtdm_initialised)
		return -ENOSYS;

	/* Sanity check: structure version */
	if (!XENO_ASSERT(RTDM, device->struct_version == RTDM_DEVICE_STRUCT_VER)) {
		printk(XENO_ERR "invalid rtdm_device version (%d, "
		       "required %d)\n", device->struct_version,
		       RTDM_DEVICE_STRUCT_VER);
		return -EINVAL;
	}

	/* Sanity check: proc_name specified? */
	if (!XENO_ASSERT(RTDM, device->proc_name)) {
		printk(XENO_ERR "no vfile (/proc) name specified for RTDM device\n");
		return -EINVAL;
	}

	switch (device->device_flags & RTDM_DEVICE_TYPE_MASK) {
	case RTDM_NAMED_DEVICE:
		/* Sanity check: any open handler? */
		if (!XENO_ASSERT(RTDM, ANY_HANDLER(*device, open))) {
			printk(XENO_ERR "missing open handler for RTDM device\n");
			return -EINVAL;
		}
		if (device->open_rt &&
		    device->socket_rt != (void *)rtdm_no_support)
			printk(XENO_ERR "RT open handler is deprecated, "
			       "RTDM driver requires update\n");
		SET_DEFAULT_OP_IF_NULL(*device, open);
		SET_DEFAULT_OP(*device, socket);
		break;

	case RTDM_PROTOCOL_DEVICE:
		/* Sanity check: any socket handler? */
		if (!XENO_ASSERT(RTDM, ANY_HANDLER(*device, socket))) {
			printk(XENO_ERR "missing socket handler for RTDM device\n");
			return -EINVAL;
		}
		if (device->socket_rt &&
		    device->socket_rt != (void *)rtdm_no_support)
			printk(XENO_ERR "RT socket creation handler is "
			       "deprecated, RTDM driver requires update\n");
		SET_DEFAULT_OP_IF_NULL(*device, socket);
		SET_DEFAULT_OP(*device, open);
		break;

	default:
		return -EINVAL;
	}

	/* Sanity check: non-RT close handler?
	 * (Always required for forced cleanup) */
	if (!device->ops.close_nrt) {
		printk(XENO_ERR "missing non-RT close handler for RTDM device\n");
		return -EINVAL;
	}
	if (device->ops.close_rt &&
	    device->ops.close_rt != (void *)rtdm_no_support)
		printk(XENO_ERR "RT close handler is deprecated, RTDM driver "
		       "requires update\n");
	else
		device->ops.close_rt = (void *)rtdm_no_support;

	SET_DEFAULT_OP_IF_NULL(device->ops, ioctl);
	SET_DEFAULT_OP_IF_NULL(device->ops, read);
	SET_DEFAULT_OP_IF_NULL(device->ops, write);
	SET_DEFAULT_OP_IF_NULL(device->ops, recvmsg);
	SET_DEFAULT_OP_IF_NULL(device->ops, sendmsg);
	if (!device->ops.select_bind)
		device->ops.select_bind = rtdm_select_bind_no_support;

	atomic_set(&device->reserved.refcount, 0);
	device->reserved.exclusive_context = NULL;

	if (device->device_flags & RTDM_EXCLUSIVE) {
		device->reserved.exclusive_context =
		    kmalloc(sizeof(struct rtdm_dev_context) +
			    device->context_size, GFP_KERNEL);
		if (!device->reserved.exclusive_context) {
			printk(XENO_ERR "no memory for exclusive context of RTDM device "
			       "(context size: %ld)\n",
				 (long)device->context_size);
			return -ENOMEM;
		}
		/* mark exclusive context as unused */
		device->reserved.exclusive_context->device = NULL;
	}

	down(&nrt_dev_lock);

	trace_cobalt_device_register(device);

	if ((device->device_flags & RTDM_DEVICE_TYPE_MASK) == RTDM_NAMED_DEVICE) {
		hashkey =
		    get_name_hash(device->device_name, RTDM_MAX_DEVNAME_LEN,
				  name_hashkey_mask);

		list_for_each(entry, &rtdm_named_devices[hashkey]) {
			existing_dev =
			    list_entry(entry, struct rtdm_device,
				       reserved.entry);
			if (strcmp(device->device_name,
				   existing_dev->device_name) == 0) {
				ret = -EEXIST;
				goto err;
			}
		}

		ret = rtdm_proc_register_device(device);
		if (ret)
			goto err;

		xnlock_get_irqsave(&rt_dev_lock, s);
		list_add_tail(&device->reserved.entry,
			      &rtdm_named_devices[hashkey]);
		xnlock_put_irqrestore(&rt_dev_lock, s);

		up(&nrt_dev_lock);
	} else {
		id = get_proto_id(device->protocol_family,
				device->socket_type);

		trace_mark(xn_rtdm, protocol_register, "device %p "
			"protocol_family %d socket_type %d flags %d "
			"class %d sub_class %d profile_version %d "
			"driver_version %d", device,
			device->protocol_family, device->socket_type,
			device->device_flags, device->device_class,
			device->device_sub_class, device->profile_version,
			device->driver_version);

		xnlock_get_irqsave(&rt_dev_lock, s);
		ret = xnid_enter(&rtdm_protocol_devices,
				&device->reserved.id, id);
		xnlock_put_irqrestore(&rt_dev_lock, s);
		if (ret < 0)
			goto err;

		ret = rtdm_proc_register_device(device);
		if (ret) {
			xnlock_get_irqsave(&rt_dev_lock, s);
			xnid_remove(&rtdm_protocol_devices,
				&device->reserved.id);
			xnlock_put_irqrestore(&rt_dev_lock, s);
			goto err;
		}

		up(&nrt_dev_lock);
	}
	return 0;

err:
	up(&nrt_dev_lock);
	if (device->reserved.exclusive_context)
		kfree(device->reserved.exclusive_context);
	return ret;
}

EXPORT_SYMBOL_GPL(rtdm_dev_register);

/**
 * @brief Unregisters a RTDM device
 *
 * @param[in] device Pointer to structure describing the device to be
 * unregistered.
 * @param[in] poll_delay Polling delay in milliseconds to check repeatedly for
 * open instances of @a device, or 0 for non-blocking mode.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENODEV is returned if the device was not registered.
 *
 * - -EAGAIN is returned if the device is busy with open instances and 0 has
 * been passed for @a poll_delay.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 *
 * Rescheduling: never.
 */
int rtdm_dev_unregister(struct rtdm_device *device, unsigned int poll_delay)
{
	spl_t s;
	struct rtdm_device *reg_dev;
	unsigned long warned = 0;

	if (!rtdm_initialised)
		return -ENOSYS;

	if ((device->device_flags & RTDM_DEVICE_TYPE_MASK) == RTDM_NAMED_DEVICE)
		reg_dev = get_named_device(device->device_name);
	else
		reg_dev = get_protocol_device(device->protocol_family,
					      device->socket_type);
	if (!reg_dev)
		return -ENODEV;

	trace_cobalt_device_unregister(device, poll_delay);

	down(&nrt_dev_lock);
	xnlock_get_irqsave(&rt_dev_lock, s);

	while (atomic_read(&reg_dev->reserved.refcount) > 1) {
		xnlock_put_irqrestore(&rt_dev_lock, s);
		up(&nrt_dev_lock);

		if (!poll_delay) {
			rtdm_dereference_device(reg_dev);
			return -EAGAIN;
		}

		if (!__test_and_set_bit(0, &warned))
			printk(XENO_WARN "RTDM device %s still in use - waiting for"
			       "release...\n", reg_dev->device_name);
		msleep(poll_delay);
		down(&nrt_dev_lock);
		xnlock_get_irqsave(&rt_dev_lock, s);
	}

	if ((device->device_flags & RTDM_DEVICE_TYPE_MASK) == RTDM_NAMED_DEVICE)
		list_del(&reg_dev->reserved.entry);
	else
		xnid_remove(&rtdm_protocol_devices, &reg_dev->reserved.id);

	xnlock_put_irqrestore(&rt_dev_lock, s);

	rtdm_proc_unregister_device(device);

	up(&nrt_dev_lock);

	if (reg_dev->reserved.exclusive_context)
		kfree(device->reserved.exclusive_context);

	return 0;
}

EXPORT_SYMBOL_GPL(rtdm_dev_unregister);
/** @} */

int __init rtdm_dev_init(void)
{
	int err, i;

	sema_init(&nrt_dev_lock, 1);

	rtdm_apc = xnapc_alloc("deferred RTDM close", rtdm_apc_handler,
				   NULL);
	if (rtdm_apc < 0)
		return rtdm_apc;

	name_hashkey_mask = devname_hashtab_size - 1;
	if (((devname_hashtab_size & name_hashkey_mask) != 0)) {
		err = -EINVAL;
		goto err_out1;
	}

	rtdm_named_devices = (struct list_head *)
	    kmalloc(devname_hashtab_size * sizeof(struct list_head),
		    GFP_KERNEL);
	if (!rtdm_named_devices) {
		err = -ENOMEM;
		goto err_out1;
	}

	for (i = 0; i < devname_hashtab_size; i++)
		INIT_LIST_HEAD(&rtdm_named_devices[i]);

	xntree_init(&rtdm_protocol_devices);

	return 0;

err_out1:
	xnapc_free(rtdm_apc);

	return err;
}

void rtdm_dev_cleanup(void)
{
	/*
	 * Note: no need to flush the cleanup_queue as no device is allowed
	 * to deregister as long as there are references.
	 */
	xnapc_free(rtdm_apc);
	kfree(rtdm_named_devices);
}

/*@}*/
