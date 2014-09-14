/*
 * Real-Time Driver Model for Xenomai, device management
 *
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include "rtdm/internal.h"
#include <trace/events/cobalt-rtdm.h>

/**
 * @addtogroup rtdm_driver_interface
 * @{
 */

#define RTDM_DEVICE_MAGIC	0x82846877

struct list_head rtdm_named_devices;	/* hash table */
struct rb_root rtdm_protocol_devices;

struct semaphore nrt_dev_lock;
DEFINE_XNLOCK(rt_dev_lock);

static int enosys(void)
{
	return -ENOSYS;
}

static inline xnkey_t get_proto_id(int pf, int type)
{
	xnkey_t llpf = (unsigned int)pf;
	return (llpf << 32) | (unsigned int)type;
}

static inline void rtdm_reference_device(struct rtdm_device *device)
{
	atomic_inc(&device->refcount);
}

struct rtdm_device *__rtdm_get_named_device(const char *name, int *minor_r)
{
	struct rtdm_device *device;
	const char *p = NULL;
	int ret, minor = -1;
	xnhandle_t handle;
	char *base = NULL;
	spl_t s;

	/*
	 * First we look for an exact match. If this fails, we look
	 * for a device minor specification. If we find one, we redo
	 * the search only looking for the device base name. The
	 * default minor value if unspecified is -1.
	 */
	for (;;) {
		ret = xnregistry_bind(name, XN_NONBLOCK, XN_RELATIVE, &handle);
		if (base)
			kfree(base);
		if (ret != -EWOULDBLOCK)
			break;
		if (p)	/* Look for minor only once. */
			return NULL;
		p = name + strlen(name);
		while (--p >= name) {
			if (!isdigit(*p))
				break;
		}
		if (p < name)	/* no minor spec. */
			return NULL;
		if (p[1] == '\0')
			return NULL;
		ret = kstrtoint(p + 1, 10, &minor);
		if (ret || minor < 0)
			return NULL;
		base = kstrdup(name, GFP_KERNEL);
		if (base == NULL)
			return NULL;
		if (*p == '@')
			base[p - name] = '\0';
		else
			base[p - name + 1] = '\0';
		name = base;
	}

	xnlock_get_irqsave(&rt_dev_lock, s);

	device = xnregistry_lookup(handle, NULL);
	if (device) {
		if (device->magic == RTDM_DEVICE_MAGIC &&
		    ((device->class->device_flags & RTDM_MINOR) != 0 ||
		     minor < 0)) {
			rtdm_reference_device(device);
			*minor_r = minor;
		} else
			device = NULL;
	}

	xnlock_put_irqrestore(&rt_dev_lock, s);

	return device;
}

struct rtdm_device *
__rtdm_get_protocol_device(int protocol_family, int socket_type)
{
	struct rtdm_device *device = NULL;
	struct xnid *xnid;
	xnkey_t id;
	spl_t s;

	id = get_proto_id(protocol_family, socket_type);

	xnlock_get_irqsave(&rt_dev_lock, s);

	xnid = xnid_fetch(&rtdm_protocol_devices, id);
	if (xnid) {
		device = container_of(xnid, struct rtdm_device, proto.id);
		rtdm_reference_device(device);
	}

	xnlock_put_irqrestore(&rt_dev_lock, s);

	return device;
}

/**
 * @ingroup rtdm_driver_interface
 * @defgroup rtdm_device_register Device Registration Services
 * @{
 */

static char *rtdm_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "rtdm/%s", dev_name(dev));
}

static int register_device_class(struct rtdm_device_class *class)
{
	struct class *kclass;
	dev_t rdev;
	int ret;

	if (class->profile_info.magic == RTDM_CLASS_MAGIC) {
		atomic_inc(&class->refcount);
		return 0;
	}

	if (class->profile_info.magic != ~RTDM_CLASS_MAGIC)
		return -EINVAL;

	switch (class->device_flags & RTDM_DEVICE_TYPE_MASK) {
	case RTDM_NAMED_DEVICE:
	case RTDM_PROTOCOL_DEVICE:
		break;
	default:
		return -EINVAL;
	}

	if (class->device_count <= 0)
		return -EINVAL;

	if ((class->device_flags & RTDM_NAMED_DEVICE) == 0)
		goto done;

	ret = alloc_chrdev_region(&rdev, 0, class->device_count,
				  class->profile_info.name);
	if (ret) {
		printk(XENO_WARN "cannot allocate chrdev region %s[0..%d]\n",
		       class->profile_info.name, class->device_count - 1);
		return ret;
	}

	cdev_init(&class->named.cdev, &rtdm_dumb_fops);
	ret = cdev_add(&class->named.cdev, rdev, class->device_count);
	if (ret)
		goto fail_cdev;

	kclass = class_create(THIS_MODULE, class->profile_info.name);
	if (IS_ERR(kclass)) {
		printk(XENO_WARN "cannot create device class %s\n",
		       class->profile_info.name);
		ret = PTR_ERR(kclass);
		goto fail_class;
	}
	kclass->devnode = rtdm_devnode;

	class->named.kclass = kclass;
	class->named.major = MAJOR(rdev);
	atomic_set(&class->refcount, 1);
done:
	class->profile_info.magic = RTDM_CLASS_MAGIC;

	return 0;

fail_class:
	cdev_del(&class->named.cdev);
fail_cdev:
	unregister_chrdev_region(rdev, class->device_count);

	return ret;
}

static void unregister_device_class(struct rtdm_device_class *class)
{
	XENO_BUGON(COBALT, class->profile_info.magic != RTDM_CLASS_MAGIC);

	if (!atomic_dec_and_test(&class->refcount))
		return;

	if (class->device_flags & RTDM_NAMED_DEVICE) {
		class_destroy(class->named.kclass);
		cdev_del(&class->named.cdev);
		unregister_chrdev_region(MKDEV(class->named.major, 0),
					 class->device_count);
	}
}

/**
 * @brief Register a RTDM device
 *
 * The device descriptor is initialized and registered in the RTDM
 * namespace.
 *
 * @param[in] device Pointer to the device descriptor register.
 * @param[in] class RTDM class the new device belongs to.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if the descriptor contains invalid
 * entries. RTDM_PROFILE_INFO() must appear in the list of
 * initializers setting up the class properties.
 *
 * - -EEXIST is returned if the specified device name of protocol ID is
 * already in use.
 *
 * - -EAGAIN is returned if some /proc entry cannot be created.
 *
 * - -ENOMEM is returned if a memory allocation failed in the process
 * of registering the device.
 *
 * - -EAGAIN is returned if some /proc entry cannot be created.
 *
 * @coretags{secondary-only}
 */
int rtdm_dev_register(struct rtdm_device *device)
{
	struct rtdm_device_class *class;
	int ret, pos, major, minor;
	struct device *kdev;
	xnkey_t id;
	spl_t s;

	if (!realtime_core_enabled())
		return -ENOSYS;

	down(&nrt_dev_lock);

	device->name = NULL;
	device->exclusive_context = NULL;
	class = device->class;
	pos = atomic_read(&class->refcount);
	ret = register_device_class(class);
	if (ret) {
		up(&nrt_dev_lock);
		return ret;
	}

	device->ops = class->ops;
	if (class->device_flags & RTDM_NAMED_DEVICE)
		device->ops.socket = (typeof(device->ops.socket))enosys;
	else
		device->ops.open = (typeof(device->ops.open))enosys;

	device->ops.close = __rt_dev_close; /* Interpose on driver's handler. */
	atomic_set(&device->refcount, 0);

	if (class->device_flags & RTDM_EXCLUSIVE) {
		device->exclusive_context =
			kmalloc(sizeof(struct rtdm_dev_context) +
				class->context_size, GFP_KERNEL);
		if (device->exclusive_context == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
		/* mark exclusive context as unused */
		device->exclusive_context->device = NULL;
	}

	device->magic = RTDM_DEVICE_MAGIC;

	if (class->device_flags & RTDM_NAMED_DEVICE) {
		major = class->named.major;
		minor = pos;
		device->named.minor = minor;
		device->name = kasformat(device->label, minor);
		if (device->name == NULL) {
			ret = -ENOMEM;
			goto fail;
		}

		kdev = device_create(class->named.kclass, NULL,
				     MKDEV(major, minor),
				     device, device->label, minor);
		if (IS_ERR(kdev)) {
			ret = PTR_ERR(kdev);
			goto fail;
		}

		ret = xnregistry_enter(device->name, device,
				       &device->named.handle, NULL);
		if (ret)
			goto fail_register;

		xnlock_get_irqsave(&rt_dev_lock, s);
		list_add_tail(&device->named.entry, &rtdm_named_devices);
		xnlock_put_irqrestore(&rt_dev_lock, s);

		ret = rtdm_proc_register_device(device);
		if (ret)
			goto fail_proc;

	} else {
		device->name = kstrdup(device->label, GFP_KERNEL);
		if (device->name == NULL) {
			ret = -ENOMEM;
			goto fail;
		}
		id = get_proto_id(class->protocol_family, class->socket_type);
		xnlock_get_irqsave(&rt_dev_lock, s);
		ret = xnid_enter(&rtdm_protocol_devices, &device->proto.id, id);
		xnlock_put_irqrestore(&rt_dev_lock, s);
		if (ret < 0)
			goto fail;

		ret = rtdm_proc_register_device(device);
		if (ret) {
			xnlock_get_irqsave(&rt_dev_lock, s);
			xnid_remove(&rtdm_protocol_devices, &device->proto.id);
			xnlock_put_irqrestore(&rt_dev_lock, s);
			goto fail;
		}
	}

	up(&nrt_dev_lock);

	trace_cobalt_device_register(device);

	return 0;
fail_proc:
	xnregistry_remove(device->named.handle);
fail_register:
	device_destroy(class->named.kclass, MKDEV(major, minor));
fail:
	unregister_device_class(class);

	up(&nrt_dev_lock);

	if (device->name)
		kfree(device->name);

	if (device->exclusive_context)
		kfree(device->exclusive_context);

	return ret;
}
EXPORT_SYMBOL_GPL(rtdm_dev_register);

/**
 * @brief Unregister a RTDM device
 *
 * The device descriptor removed from the RTDM namespace.
 *
 * @param[in] device Pointer to the device descriptor.
 * @param[in] poll_delay Polling delay in milliseconds to check repeatedly for
 * open instances of @a device, or 0 for non-blocking mode.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EAGAIN is returned if the device is busy with open instances and
 * 0 has been passed for @a poll_delay.
 *
 * @coretags{secondary-only}
 */
int rtdm_dev_unregister(struct rtdm_device *device, unsigned int poll_delay)
{
	struct rtdm_device_class *class = device->class;
	xnhandle_t handle = XN_NO_HANDLE;
	unsigned long warned = 0;
	spl_t s;

	rtdm_reference_device(device);

	trace_cobalt_device_unregister(device, poll_delay);

	down(&nrt_dev_lock);
	xnlock_get_irqsave(&rt_dev_lock, s);

	while (atomic_read(&device->refcount) > 1) {
		xnlock_put_irqrestore(&rt_dev_lock, s);
		up(&nrt_dev_lock);

		if (!poll_delay) {
			rtdm_dereference_device(device);
			return -EAGAIN;
		}

		if (!__test_and_set_bit(0, &warned))
			printk(XENO_WARN "RTDM device %s still in use - waiting for"
			       " release...\n", device->name);
		msleep(poll_delay);
		down(&nrt_dev_lock);
		xnlock_get_irqsave(&rt_dev_lock, s);
	}

	if (class->device_flags & RTDM_NAMED_DEVICE) {
		handle = device->named.handle;
		list_del(&device->named.entry);
	} else
		xnid_remove(&rtdm_protocol_devices, &device->proto.id);

	xnlock_put_irqrestore(&rt_dev_lock, s);

	rtdm_proc_unregister_device(device);

	if (handle) {
		xnregistry_remove(handle);
		device_destroy(class->named.kclass,
			       MKDEV(class->named.major,
				     device->named.minor));
	}

	unregister_device_class(class);

	up(&nrt_dev_lock);

	if (device->exclusive_context)
		kfree(device->exclusive_context);

	kfree(device->name);

	return 0;
}
EXPORT_SYMBOL_GPL(rtdm_dev_unregister);

/** @} */

int __init rtdm_dev_init(void)
{
	sema_init(&nrt_dev_lock, 1);

	INIT_LIST_HEAD(&rtdm_named_devices);
	xntree_init(&rtdm_protocol_devices);

	return 0;
}

void rtdm_dev_cleanup(void)
{
	/*
	 * Note: no need to flush the cleanup_queue as no device is allowed
	 * to deregister as long as there are references.
	 */
}

/*@}*/
