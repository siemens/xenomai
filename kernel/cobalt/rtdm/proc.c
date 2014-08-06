/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joerg.langenberg@gmx.net>.
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
#include "rtdm/internal.h"
#include <cobalt/kernel/vfile.h>

struct xnvfile_directory rtdm_vfroot;	/* /proc/xenomai/rtdm */

struct vfile_device_data {
	struct rtdm_device *curr;
};

static int get_nrt_lock(struct xnvfile *vfile)
{
	return down_interruptible(&nrt_dev_lock) ? -ERESTARTSYS : 0;
}

static void put_nrt_lock(struct xnvfile *vfile)
{
	up(&nrt_dev_lock);
}

static struct xnvfile_lock_ops lockops = {
	.get = get_nrt_lock,
	.put = put_nrt_lock,
};

static void *named_next(struct xnvfile_regular_iterator *it)
{
	struct vfile_device_data *priv = xnvfile_iterator_priv(it);
	struct rtdm_device *device = priv->curr;
	struct list_head *next;

	next = device->reserved.entry.next;
	if (next == &rtdm_named_devices) {
		priv->curr = NULL; /* all done. */
		goto out;
	}

	priv->curr = list_entry(next, struct rtdm_device, reserved.entry);

  out:
	return priv->curr;
}

static void *named_begin(struct xnvfile_regular_iterator *it)
{
	struct vfile_device_data *priv = xnvfile_iterator_priv(it);
	struct rtdm_device *device;
	loff_t pos = 0;

	list_for_each_entry(device, &rtdm_named_devices, reserved.entry)
		if (pos++ >= it->pos)
			break;

	if (&device->reserved.entry == &rtdm_named_devices)
		return NULL;	/* End of list. */

	priv->curr = device;	/* Skip head. */

	if (pos == 1)
		/* Output the header once, only if some device follows. */
		xnvfile_puts(it, "Name\t\t\t\tDriver\t\t/proc\n");

	return priv->curr;
}

static int named_show(struct xnvfile_regular_iterator *it, void *data)
{
	struct rtdm_device *device = data;

	xnvfile_printf(it, "%-31s\t%-15s\t%s\n", device->device_name,
		device->driver_name, device->proc_name);

	return 0;
}

static struct xnvfile_regular_ops named_vfile_ops = {
	.begin = named_begin,
	.next = named_next,
	.show = named_show,
};

static struct xnvfile_regular named_vfile = {
	.privsz = sizeof(struct vfile_device_data),
	.ops = &named_vfile_ops,
	.entry = { .lockops = &lockops }
};

struct vfile_proto_data {
	struct rtdm_device *curr;
};

static void *proto_next(struct xnvfile_regular_iterator *it)
{
	struct vfile_proto_data *priv = xnvfile_iterator_priv(it);

	return priv->curr = xnid_next_entry(priv->curr, reserved.id);
}

static void *proto_begin(struct xnvfile_regular_iterator *it)
{

	struct vfile_proto_data *priv = xnvfile_iterator_priv(it);
	struct rtdm_device *dev = NULL;
	loff_t pos = 0;

	xntree_for_each_entry(dev, &rtdm_protocol_devices, reserved.id)
		if (pos++ >= it->pos)
			break;

	if (dev == NULL)
		return NULL;	/* Empty */

	priv->curr = dev;

	if (pos == 1)
		/* Output the header once, only if some device follows. */
		xnvfile_puts(it, "Name\t\t\t\tDriver\t\t/proc\n");

	return priv->curr;
}

static int proto_show(struct xnvfile_regular_iterator *it, void *data)
{
	struct rtdm_device *device = data;
	char pnum[32];

	ksformat(pnum, sizeof(pnum), "%u:%u",
		 device->protocol_family, device->socket_type);

	xnvfile_printf(it, "%-31s\t%-15s\t%s\n",
		       pnum, device->driver_name,
		       device->proc_name);
	return 0;
}

static struct xnvfile_regular_ops proto_vfile_ops = {
	.begin = proto_begin,
	.next = proto_next,
	.show = proto_show,
};

static struct xnvfile_regular proto_vfile = {
	.privsz = sizeof(struct vfile_proto_data),
	.ops = &proto_vfile_ops,
	.entry = { .lockops = &lockops }
};

static void *openfd_begin(struct xnvfile_regular_iterator *it)
{
	if (it->pos == 0)
		return VFILE_SEQ_START;

	return it->pos <= RTDM_FD_MAX ? it : NULL;
}

static void *openfd_next(struct xnvfile_regular_iterator *it)
{
	if (it->pos > RTDM_FD_MAX)
		return NULL;

	return it;
}

static int openfd_show(struct xnvfile_regular_iterator *it, void *data)
{
	struct rtdm_dev_context *context;
	struct rtdm_device *device;
	struct rtdm_process *owner;
	int close_lock_count, i;
	struct rtdm_fd *fd;

	if (data == NULL) {
		xnvfile_puts(it, "Index\tLocked\tDevice\t\t\t\tOwner [PID]\n");
		return 0;
	}

	i = (int)it->pos - 1;

	fd = rtdm_fd_get(&__xnsys_global_ppd, i, RTDM_FD_MAGIC);
	if (IS_ERR(fd))
		return VFILE_SEQ_SKIP;

	context = rtdm_fd_to_context(fd);
	close_lock_count = fd->refs;
	device = context->device;
	owner = context->reserved.owner;

	xnvfile_printf(it, "%d\t%d\t%-31s %s [%d]\n", i,
		       close_lock_count,
		       (device->device_flags & RTDM_NAMED_DEVICE) ?
		       device->device_name : device->proc_name,
		       owner ? owner->name : "<kernel>",
		       owner ? owner->pid : 0);

	rtdm_fd_put(fd);

	return 0;
}

static ssize_t openfd_store(struct xnvfile_input *input)
{
	ssize_t ret, cret;
	long val;

	ret = xnvfile_get_integer(input, &val);
	if (ret < 0)
		return ret;

	cret = rtdm_fd_close(&__xnsys_global_ppd, (int)val, RTDM_FD_MAGIC);
	if (cret < 0)
		return cret;

	return ret;
}

static struct xnvfile_regular_ops openfd_vfile_ops = {
	.begin = openfd_begin,
	.next = openfd_next,
	.show = openfd_show,
	.store = openfd_store,
};

static struct xnvfile_regular openfd_vfile = {
	.ops = &openfd_vfile_ops,
	.entry = { .lockops = &lockops }
};

static int allfd_vfile_show(struct xnvfile_regular_iterator *it, void *data)
{
	xnvfile_printf(it, "total=%d:open=%d:free=%d\n", RTDM_FD_MAX,
		       open_fildes, RTDM_FD_MAX - open_fildes);
	return 0;
}

static struct xnvfile_regular_ops allfd_vfile_ops = {
	.show = allfd_vfile_show,
};

static struct xnvfile_regular allfd_vfile = {
	.ops = &allfd_vfile_ops,
};

static int devinfo_vfile_show(struct xnvfile_regular_iterator *it, void *data)
{
	struct rtdm_device *device;

	if (down_interruptible(&nrt_dev_lock))
		return -ERESTARTSYS;

	/*
	 * As the device may have disappeared while the handler was called,
	 * first match the pointer against registered devices.
	 */
	list_for_each_entry(device, &rtdm_named_devices, reserved.entry)
		if (device == xnvfile_priv(it->vfile))
			goto found;

	xntree_for_each_entry(device, &rtdm_protocol_devices, reserved.id)
		if (device == xnvfile_priv(it->vfile))
			goto found;

	up(&nrt_dev_lock);
	return -ENODEV;

found:
	xnvfile_printf(it, "driver:\t\t%s\nversion:\t%d.%d.%d\n",
		       device->driver_name,
		       RTDM_DRIVER_MAJOR_VER(device->driver_version),
		       RTDM_DRIVER_MINOR_VER(device->driver_version),
		       RTDM_DRIVER_PATCH_VER(device->driver_version));

	xnvfile_printf(it, "peripheral:\t%s\nprovider:\t%s\n",
		       device->peripheral_name, device->provider_name);

	xnvfile_printf(it, "class:\t\t%d\nsub-class:\t%d\n",
		       device->device_class, device->device_sub_class);

	xnvfile_printf(it, "flags:\t\t%s%s%s\n",
		       (device->device_flags & RTDM_EXCLUSIVE) ?
		       "EXCLUSIVE  " : "",
		       (device->device_flags & RTDM_NAMED_DEVICE) ?
		       "NAMED_DEVICE  " : "",
		       (device->device_flags & RTDM_PROTOCOL_DEVICE) ?
		       "PROTOCOL_DEVICE  " : "");

	xnvfile_printf(it, "lock count:\t%d\n",
		       atomic_read(&device->reserved.refcount));

	up(&nrt_dev_lock);
	return 0;
}

static struct xnvfile_regular_ops devinfo_vfile_ops = {
	.show = devinfo_vfile_show,
};

int rtdm_proc_register_device(struct rtdm_device *device)
{
	int ret;

	ret = xnvfile_init_dir(device->proc_name,
			       &device->vfroot, &rtdm_vfroot);
	if (ret)
		goto err_out;

	memset(&device->info_vfile, 0, sizeof(device->info_vfile));
	device->info_vfile.ops = &devinfo_vfile_ops;

	ret = xnvfile_init_regular("information", &device->info_vfile,
				   &device->vfroot);
	if (ret) {
		xnvfile_destroy_dir(&device->vfroot);
		goto err_out;
	}

	xnvfile_priv(&device->info_vfile) = device;

	return 0;

      err_out:
	printk(XENO_ERR "error while creating RTDM device vfile\n");
	return ret;
}

void rtdm_proc_unregister_device(struct rtdm_device *device)
{
	xnvfile_destroy_regular(&device->info_vfile);
	xnvfile_destroy_dir(&device->vfroot);
}

int __init rtdm_proc_init(void)
{
	int ret;

	/* Initialise vfiles */
	ret = xnvfile_init_dir("rtdm", &rtdm_vfroot, &nkvfroot);
	if (ret)
		goto error;

	ret = xnvfile_init_regular("named_devices", &named_vfile, &rtdm_vfroot);
	if (ret)
		goto error;

	ret = xnvfile_init_regular("protocol_devices", &proto_vfile, &rtdm_vfroot);
	if (ret)
		goto error;

	ret = xnvfile_init_regular("open_fildes", &openfd_vfile, &rtdm_vfroot);
	if (ret)
		goto error;

	ret = xnvfile_init_regular("fildes", &allfd_vfile, &rtdm_vfroot);
	if (ret)
		goto error;

	return 0;

error:
	rtdm_proc_cleanup();
	return ret;
}

void rtdm_proc_cleanup(void)
{
	xnvfile_destroy_regular(&allfd_vfile);
	xnvfile_destroy_regular(&openfd_vfile);
	xnvfile_destroy_regular(&proto_vfile);
	xnvfile_destroy_regular(&named_vfile);
	xnvfile_destroy_dir(&rtdm_vfroot);
}
