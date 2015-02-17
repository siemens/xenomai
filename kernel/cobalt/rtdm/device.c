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
#include <linux/notifier.h>
#include "rtdm/internal.h"
#include <cobalt/kernel/init.h>
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

static struct rb_root protocol_devices;

static DEFINE_MUTEX(register_lock);

static struct class *rtdm_class;

static int enosys(void)
{
	return -ENOSYS;
}

void __rtdm_put_device(struct rtdm_device *dev)
{
	secondary_mode_only();

	if (atomic_dec_and_test(&dev->refcount))
		wake_up(&dev->putwq);
}

static inline xnkey_t get_proto_id(int pf, int type)
{
	xnkey_t llpf = (unsigned int)pf;
	return (llpf << 32) | (unsigned int)type;
}

struct rtdm_device *__rtdm_get_namedev(const char *path)
{
	struct rtdm_device *dev;
	xnhandle_t handle;
	int ret;

	secondary_mode_only();

	/* skip common /dev prefix */
	if (strncmp(path, "/dev/", 5) == 0)
		path += 5;

	/* skip RTDM devnode root */
	if (strncmp(path, "rtdm/", 5) == 0)
		path += 5;

	ret = xnregistry_bind(path, XN_NONBLOCK, XN_RELATIVE, &handle);
	if (ret)
		return NULL;

	mutex_lock(&register_lock);

	dev = xnregistry_lookup(handle, NULL);
	if (dev && dev->magic == RTDM_DEVICE_MAGIC)
		__rtdm_get_device(dev);
	else
		dev = NULL;

	mutex_unlock(&register_lock);

	return dev;
}

struct rtdm_device *__rtdm_get_protodev(int protocol_family, int socket_type)
{
	struct rtdm_device *dev = NULL;
	struct xnid *xnid;
	xnkey_t id;

	secondary_mode_only();

	id = get_proto_id(protocol_family, socket_type);

	mutex_lock(&register_lock);

	xnid = xnid_fetch(&protocol_devices, id);
	if (xnid) {
		dev = container_of(xnid, struct rtdm_device, proto.id);
		__rtdm_get_device(dev);
	}

	mutex_unlock(&register_lock);

	return dev;
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
	struct rtdm_device *dev = dev_get_drvdata(kdev);

	return sprintf(buf, "%d,%d\n",
		       dev->driver->profile_info.class_id,
		       dev->driver->profile_info.subclass_id);
}

static ssize_t refcount_show(struct device *kdev,
			     struct device_attribute *attr, char *buf)
{
	struct rtdm_device *dev = dev_get_drvdata(kdev);

	return sprintf(buf, "%d\n", atomic_read(&dev->refcount));
}

#define cat_count(__buf, __str)			\
	({					\
		int __ret = sizeof(__str) - 1;	\
		strcat(__buf, __str);		\
		__ret;				\
	})

static ssize_t flags_show(struct device *kdev,
			  struct device_attribute *attr, char *buf)
{
	struct rtdm_device *dev = dev_get_drvdata(kdev);
	struct rtdm_driver *drv = dev->driver;

	return sprintf(buf, "%#x\n", drv->device_flags);

}

static ssize_t type_show(struct device *kdev,
			 struct device_attribute *attr, char *buf)
{
	struct rtdm_device *dev = dev_get_drvdata(kdev);
	struct rtdm_driver *drv = dev->driver;
	int ret;

	if (drv->device_flags & RTDM_NAMED_DEVICE)
		ret = cat_count(buf, "named\n");
	else
		ret = cat_count(buf, "protocol\n");

	return ret;

}

#ifdef ATTRIBUTE_GROUPS

static DEVICE_ATTR_RO(profile);
static DEVICE_ATTR_RO(refcount);
static DEVICE_ATTR_RO(flags);
static DEVICE_ATTR_RO(type);

static struct attribute *rtdm_attrs[] = {
	&dev_attr_profile.attr,
	&dev_attr_refcount.attr,
	&dev_attr_flags.attr,
	&dev_attr_type.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rtdm);

#else /* !ATTRIBUTE_GROUPS */

/*
 * Cope with legacy sysfs attributes. Scheduled for removal when 3.10
 * is at EOL for us.
 */
static struct device_attribute rtdm_attrs[] = {
	DEVICE_ATTR_RO(profile),
	DEVICE_ATTR_RO(refcount),
	DEVICE_ATTR_RO(flags),
	DEVICE_ATTR_RO(type),
	__ATTR_NULL 
};

#define dev_groups   dev_attrs
#define rtdm_groups  rtdm_attrs

#endif /* !ATTRIBUTE_GROUPS */

static int state_change_notifier(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct rtdm_driver *drv;
	int ret;

	drv = container_of(nb, struct rtdm_driver, nb_statechange);

	switch (action) {
	case COBALT_STATE_WARMUP:
		if (drv->smops.start == NULL)
			return NOTIFY_DONE;
		ret = drv->smops.start(drv);
		if (ret)
			printk(XENO_WARNING
			       "failed starting driver %s (%d)\n",
			       drv->profile_info.name, ret);
		break;
	case COBALT_STATE_TEARDOWN:
		if (drv->smops.stop == NULL)
			return NOTIFY_DONE;
		ret = drv->smops.stop(drv);
		if (ret)
			printk(XENO_WARNING
			       "failed stopping driver %s (%d)\n",
			       drv->profile_info.name, ret);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

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
		printk(XENO_WARNING "cannot allocate chrdev region %s[0..%d]\n",
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
	drv->nb_statechange.notifier_call = state_change_notifier;
	drv->nb_statechange.priority = 0;
	cobalt_add_notifier_chain(&drv->nb_statechange);
	drv->profile_info.magic = RTDM_CLASS_MAGIC;

	return 0;

fail_cdev:
	unregister_chrdev_region(rdev, drv->device_count);

	return ret;
}

static void unregister_driver(struct rtdm_driver *drv)
{
	XENO_BUG_ON(COBALT, drv->profile_info.magic != RTDM_CLASS_MAGIC);

	if (!atomic_dec_and_test(&drv->refcount))
		return;

	cobalt_remove_notifier_chain(&drv->nb_statechange);
	
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
 * @param[in] dev Device descriptor.
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
int rtdm_dev_register(struct rtdm_device *dev)
{
	int ret, pos, major, minor;
	struct device *kdev = NULL;
	struct rtdm_driver *drv;
	xnkey_t id;
	dev_t rdev;

	secondary_mode_only();

	if (!realtime_core_enabled())
		return -ENOSYS;

	mutex_lock(&register_lock);

	dev->name = NULL;
	drv = dev->driver;
	pos = atomic_read(&drv->refcount);
	ret = register_driver(drv);
	if (ret) {
		mutex_unlock(&register_lock);
		return ret;
	}

	dev->ops = drv->ops;
	if (drv->device_flags & RTDM_NAMED_DEVICE)
		dev->ops.socket = (typeof(dev->ops.socket))enosys;
	else
		dev->ops.open = (typeof(dev->ops.open))enosys;

	init_waitqueue_head(&dev->putwq);
	dev->ops.close = __rtdm_dev_close; /* Interpose on driver's handler. */
	atomic_set(&dev->refcount, 0);

	if (drv->device_flags & RTDM_FIXED_MINOR) {
		minor = dev->minor;
		if (minor < 0 || minor >= drv->device_count) {
			ret = -EINVAL;
			goto fail;
		}
	} else
		dev->minor = minor = pos;

	if (drv->device_flags & RTDM_NAMED_DEVICE) {
		major = drv->named.major;
		dev->name = kasformat(dev->label, minor);
		if (dev->name == NULL) {
			ret = -ENOMEM;
			goto fail;
		}

		ret = xnregistry_enter(dev->name, dev,
				       &dev->named.handle, NULL);
		if (ret)
			goto fail;

		rdev = MKDEV(major, minor);
		kdev = device_create(rtdm_class, NULL, rdev,
				     dev, dev->label, minor);
		if (IS_ERR(kdev)) {
			xnregistry_remove(dev->named.handle);
			ret = PTR_ERR(kdev);
			goto fail;
		}
	} else {
		dev->name = kstrdup(dev->label, GFP_KERNEL);
		if (dev->name == NULL) {
			ret = -ENOMEM;
			goto fail;
		}

		rdev = MKDEV(0, 0);
		kdev = device_create(rtdm_class, NULL, rdev,
				     dev, dev->name);
		if (IS_ERR(kdev)) {
			ret = PTR_ERR(kdev);
			goto fail;
		}

		id = get_proto_id(drv->protocol_family, drv->socket_type);
		ret = xnid_enter(&protocol_devices, &dev->proto.id, id);
		if (ret < 0)
			goto fail;
	}

	dev->rdev = rdev;
	dev->kdev = kdev;
	dev->magic = RTDM_DEVICE_MAGIC;

	mutex_unlock(&register_lock);

	trace_cobalt_device_register(dev);

	return 0;
fail:
	if (kdev)
		device_destroy(rtdm_class, rdev);

	unregister_driver(drv);

	mutex_unlock(&register_lock);

	if (dev->name)
		kfree(dev->name);

	return ret;
}
EXPORT_SYMBOL_GPL(rtdm_dev_register);

/**
 * @brief Unregister a RTDM device
 *
 * Removes the device from the RTDM namespace. This routine waits until
 * all connections to @a device have been closed prior to unregistering.
 *
 * @param[in] dev Device descriptor.
 *
 * @coretags{secondary-only}
 */
void rtdm_dev_unregister(struct rtdm_device *dev)
{
	struct rtdm_driver *drv = dev->driver;

	secondary_mode_only();

	trace_cobalt_device_unregister(dev);

	/* Lock out any further connection. */
	dev->magic = ~RTDM_DEVICE_MAGIC;

	/* Then wait for the ongoing connections to finish. */
	wait_event(dev->putwq,
		   atomic_read(&dev->refcount) == 0);

	mutex_lock(&register_lock);

	if (drv->device_flags & RTDM_NAMED_DEVICE)
		xnregistry_remove(dev->named.handle);
	else
		xnid_remove(&protocol_devices, &dev->proto.id);

	device_destroy(rtdm_class, dev->rdev);

	unregister_driver(drv);

	mutex_unlock(&register_lock);

	kfree(dev->name);
}
EXPORT_SYMBOL_GPL(rtdm_dev_unregister);

/** @} */

int __init rtdm_init(void)
{
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
