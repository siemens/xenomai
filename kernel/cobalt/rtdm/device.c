/*
 * Real-Time Driver Model for Xenomai, device management
 *
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>
 * Copyright (C) 2014 Philippe Gerum <rpm@xenomai.org>
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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>
#include "rtdm/internal.h"
#include <trace/events/cobalt-rtdm.h>

/**
 * @ingroup rtdm
 * @defgroup rtdm_profiles Device Profiles
 *
 * Pre-defined classes of real-time devices
 *
 * Device profiles define which operation handlers a driver of a
 * certain class of devices has to implement, which name or protocol
 * it has to register, which IOCTLs it has to provide, and further
 * details. Sub-classes can be defined in order to extend a device
 * profile with more hardware-specific functions.
 */

/**
 * @addtogroup rtdm_driver_interface
 * @{
 */

#define RTDM_DEVICE_MAGIC	0x82846877

static struct list_head named_devices;
static struct rb_root protocol_devices;
static DEFINE_MUTEX(register_lock);
DEFINE_PRIVATE_XNLOCK(rt_dev_lock);

static struct class *rtdm_class;

static int enosys(void)
{
	return -ENOSYS;
}

void __rtdm_put_device(struct rtdm_device *device)
{
	secondary_mode_only();

	if (atomic_dec_and_test(&device->refcount))
		wake_up(&device->putwq);
}

static inline xnkey_t get_proto_id(int pf, int type)
{
	xnkey_t llpf = (unsigned int)pf;
	return (llpf << 32) | (unsigned int)type;
}

struct rtdm_device *__rtdm_get_namedev(const char *path)
{
	struct rtdm_device *device;
	xnhandle_t handle;
	int ret;
	spl_t s;

	/* skip common /dev prefix */
	if (strncmp(path, "/dev/", 5) == 0)
		path += 5;

	/* skip RTDM devnode root */
	if (strncmp(path, "rtdm/", 5) == 0)
		path += 5;

	ret = xnregistry_bind(path, XN_NONBLOCK, XN_RELATIVE, &handle);
	if (ret)
		return NULL;

	xnlock_get_irqsave(&rt_dev_lock, s);

	device = xnregistry_lookup(handle, NULL);
	if (device && device->magic == RTDM_DEVICE_MAGIC)
		__rtdm_get_device(device);
	else
		device = NULL;

	xnlock_put_irqrestore(&rt_dev_lock, s);

	return device;
}

struct rtdm_device *__rtdm_get_protodev(int protocol_family, int socket_type)
{
	struct rtdm_device *device = NULL;
	struct xnid *xnid;
	xnkey_t id;
	spl_t s;

	id = get_proto_id(protocol_family, socket_type);

	xnlock_get_irqsave(&rt_dev_lock, s);

	xnid = xnid_fetch(&protocol_devices, id);
	if (xnid) {
		device = container_of(xnid, struct rtdm_device, proto.id);
		__rtdm_get_device(device);
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

static ssize_t profile_show(struct device *kdev,
			    struct device_attribute *attr, char *buf)
{
	struct rtdm_device *device = dev_get_drvdata(kdev);

	return sprintf(buf, "%d,%d\n",
		       device->driver->profile_info.class_id,
		       device->driver->profile_info.subclass_id);
}
static DEVICE_ATTR_RO(profile);

static ssize_t refcount_show(struct device *kdev,
			     struct device_attribute *attr, char *buf)
{
	struct rtdm_device *device = dev_get_drvdata(kdev);

	return sprintf(buf, "%d\n", atomic_read(&device->refcount));
}
static DEVICE_ATTR_RO(refcount);

#define cat_count(__buf, __str)			\
	({					\
		int __ret = sizeof(__str) - 1;	\
		strcat(__buf, __str);		\
		__ret;				\
	})

static ssize_t flags_show(struct device *kdev,
			  struct device_attribute *attr, char *buf)
{
	struct rtdm_device *device = dev_get_drvdata(kdev);
	struct rtdm_driver *drv = device->driver;

	return sprintf(buf, "%#x\n", drv->device_flags);

}
static DEVICE_ATTR_RO(flags);

static ssize_t type_show(struct device *kdev,
			 struct device_attribute *attr, char *buf)
{
	struct rtdm_device *device = dev_get_drvdata(kdev);
	struct rtdm_driver *drv = device->driver;
	int ret;

	if (drv->device_flags & RTDM_NAMED_DEVICE)
		ret = cat_count(buf, "named\n");
	else
		ret = cat_count(buf, "protocol\n");

	return ret;

}
static DEVICE_ATTR_RO(type);

static struct attribute *rtdm_attrs[] = {
	&dev_attr_profile.attr,
	&dev_attr_refcount.attr,
	&dev_attr_flags.attr,
	&dev_attr_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rtdm);

static int register_driver(struct rtdm_driver *drv)
{
	dev_t rdev;
	int ret;

	if (drv->profile_info.magic == RTDM_CLASS_MAGIC) {
		atomic_inc(&drv->refcount);
		return 0;
	}

	if (drv->profile_info.magic != ~RTDM_CLASS_MAGIC)
		return -EINVAL;

	switch (drv->device_flags & RTDM_DEVICE_TYPE_MASK) {
	case RTDM_NAMED_DEVICE:
	case RTDM_PROTOCOL_DEVICE:
		break;
	default:
		return -EINVAL;
	}

	if (drv->device_count <= 0)
		return -EINVAL;

	if ((drv->device_flags & RTDM_NAMED_DEVICE) == 0)
		goto done;

	ret = alloc_chrdev_region(&rdev, 0, drv->device_count,
				  drv->profile_info.name);
	if (ret) {
		printk(XENO_WARN "cannot allocate chrdev region %s[0..%d]\n",
		       drv->profile_info.name, drv->device_count - 1);
		return ret;
	}

	cdev_init(&drv->named.cdev, &rtdm_dumb_fops);
	ret = cdev_add(&drv->named.cdev, rdev, drv->device_count);
	if (ret)
		goto fail_cdev;

	drv->named.major = MAJOR(rdev);
	atomic_set(&drv->refcount, 1);
done:
	drv->profile_info.magic = RTDM_CLASS_MAGIC;

	return 0;

fail_cdev:
	unregister_chrdev_region(rdev, drv->device_count);

	return ret;
}

static void unregister_driver(struct rtdm_driver *drv)
{
	XENO_BUGON(COBALT, drv->profile_info.magic != RTDM_CLASS_MAGIC);

	if (!atomic_dec_and_test(&drv->refcount))
		return;

	if (drv->device_flags & RTDM_NAMED_DEVICE) {
		cdev_del(&drv->named.cdev);
		unregister_chrdev_region(MKDEV(drv->named.major, 0),
					 drv->device_count);
	}
}

/**
 * @brief Register a RTDM device
 *
 * Registers a device in the RTDM namespace.
 *
 * @param[in] device Device descriptor.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if the descriptor contains invalid
 * entries. RTDM_PROFILE_INFO() must appear in the list of
 * initializers for the driver properties.
 *
 * - -EEXIST is returned if the specified device name of protocol ID is
 * already in use.
 *
 * - -ENOMEM is returned if a memory allocation failed in the process
 * of registering the device.
 *
 * @coretags{secondary-only}
 */
int rtdm_dev_register(struct rtdm_device *device)
{
	int ret, pos, major, minor;
	struct device *kdev = NULL;
	struct rtdm_driver *drv;
	xnkey_t id;
	dev_t rdev;
	spl_t s;

	secondary_mode_only();

	if (!realtime_core_enabled())
		return -ENOSYS;

	mutex_lock(&register_lock);

	device->name = NULL;
	drv = device->driver;
	pos = atomic_read(&drv->refcount);
	ret = register_driver(drv);
	if (ret) {
		mutex_unlock(&register_lock);
		return ret;
	}

	device->ops = drv->ops;
	if (drv->device_flags & RTDM_NAMED_DEVICE)
		device->ops.socket = (typeof(device->ops.socket))enosys;
	else
		device->ops.open = (typeof(device->ops.open))enosys;

	init_waitqueue_head(&device->putwq);
	device->ops.close = __rtdm_dev_close; /* Interpose on driver's handler. */
	atomic_set(&device->refcount, 0);

	if (drv->device_flags & RTDM_FIXED_MINOR) {
		minor = device->minor;
		if (minor < 0 || minor >= drv->device_count) {
			ret = -EINVAL;
			goto fail;
		}
	} else
		device->minor = minor = pos;

	if (drv->device_flags & RTDM_NAMED_DEVICE) {
		major = drv->named.major;
		device->name = kasformat(device->label, minor);
		if (device->name == NULL) {
			ret = -ENOMEM;
			goto fail;
		}

		ret = xnregistry_enter(device->name, device,
				       &device->named.handle, NULL);
		if (ret)
			goto fail;

		rdev = MKDEV(major, minor);
		kdev = device_create(rtdm_class, NULL, rdev,
				     device, device->label, minor);
		if (IS_ERR(kdev)) {
			xnregistry_remove(device->named.handle);
			ret = PTR_ERR(kdev);
			goto fail;
		}

		xnlock_get_irqsave(&rt_dev_lock, s);
		list_add_tail(&device->named.entry, &named_devices);
		xnlock_put_irqrestore(&rt_dev_lock, s);
	} else {
		device->name = kstrdup(device->label, GFP_KERNEL);
		if (device->name == NULL) {
			ret = -ENOMEM;
			goto fail;
		}

		rdev = MKDEV(0, 0);
		kdev = device_create(rtdm_class, NULL, rdev,
				     device, device->name);
		if (IS_ERR(kdev)) {
			ret = PTR_ERR(kdev);
			goto fail;
		}

		id = get_proto_id(drv->protocol_family, drv->socket_type);
		xnlock_get_irqsave(&rt_dev_lock, s);
		ret = xnid_enter(&protocol_devices, &device->proto.id, id);
		xnlock_put_irqrestore(&rt_dev_lock, s);
		if (ret < 0)
			goto fail;
	}

	device->rdev = rdev;
	device->kdev = kdev;
	device->magic = RTDM_DEVICE_MAGIC;

	mutex_unlock(&register_lock);

	trace_cobalt_device_register(device);

	return 0;
fail:
	if (kdev)
		device_destroy(rtdm_class, rdev);

	unregister_driver(drv);

	mutex_unlock(&register_lock);

	if (device->name)
		kfree(device->name);

	return ret;
}
EXPORT_SYMBOL_GPL(rtdm_dev_register);

/**
 * @brief Unregister a RTDM device
 *
 * Removes the device from the RTDM namespace. This routine waits until
 * all connections to @a device have been closed prior to unregistering.
 *
 * @param[in] device Device descriptor.
 *
 * @coretags{secondary-only}
 */
void rtdm_dev_unregister(struct rtdm_device *device)
{
	struct rtdm_driver *drv = device->driver;
	xnhandle_t handle = XN_NO_HANDLE;
	spl_t s;

	secondary_mode_only();

	trace_cobalt_device_unregister(device);

	/* Lock out any further connection. */
	device->magic = ~RTDM_DEVICE_MAGIC;

	/* Then wait for the ongoing connections to finish. */
	wait_event(device->putwq,
		   atomic_read(&device->refcount) == 0);

	mutex_lock(&register_lock);
	xnlock_get_irqsave(&rt_dev_lock, s);

	if (drv->device_flags & RTDM_NAMED_DEVICE) {
		handle = device->named.handle;
		list_del(&device->named.entry);
	} else
		xnid_remove(&protocol_devices, &device->proto.id);

	xnlock_put_irqrestore(&rt_dev_lock, s);

	if (handle)
		xnregistry_remove(handle);

	device_destroy(rtdm_class, device->rdev);

	unregister_driver(drv);

	mutex_unlock(&register_lock);

	kfree(device->name);
}
EXPORT_SYMBOL_GPL(rtdm_dev_unregister);

/** @} */

int __init rtdm_init(void)
{
	INIT_LIST_HEAD(&named_devices);
	xntree_init(&protocol_devices);

	rtdm_class = class_create(THIS_MODULE, "rtdm");
	if (IS_ERR(rtdm_class)) {
		printk(XENO_ERR "cannot create RTDM sysfs class\n");
		return PTR_ERR(rtdm_class);
	}
	rtdm_class->dev_groups = rtdm_groups;
	rtdm_class->devnode = rtdm_devnode;

	return 0;
}

void rtdm_cleanup(void)
{
	class_destroy(rtdm_class);
	/*
	 * NOTE: no need to flush the cleanup_queue as no device is
	 * allowed to unregister as long as there are references.
	 */
}

/*@}*/
