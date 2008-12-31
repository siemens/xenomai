/**
 * @file
 * Comedi for RTDM, driver facilities
 *
 * @note Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
 * @note Copyright (C) 2008 Alexis Berlemont <alexis.berlemont@free.fr>
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
 * @defgroup Comedi4RTDM Comedi API.
 *
 * This is the API interface of Comedi4RTDM (kernel layer and user
 * layer)
 *
 */

/*!
 * @ingroup Comedi4RTDM
 * @defgroup driverfacilities Driver API.
 *
 * This is the API interface of Comedi provided to device drivers.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>

#include <comedi/context.h>
#include <comedi/device.h>
#include <comedi/comedi_driver.h>

/* --- Driver section --- */

/*!
 * @ingroup driverfacilities
 * @defgroup driver Driver management services
 *
 * Comedi driver registration / unregistration
 *
 * In a common Linux char driver, the developer has to register a fops
 * structure filled with callbacks for read / write / mmap / ioctl
 * operations.
 *
 * Comedi drivers do not have to implement read / write / mmap / ioctl
 * functions, these procedures are implemented in the Comedi generic
 * layer. Then, the transfers between user-space and kernel-space are
 * already managed. Comedi drivers work with commands and instructions
 * which are some kind of more dedicated read / write operations. And,
 * instead of registering a fops structure, a Comedi driver must
 * register some comedi_driver structure.
 *
 * Before registrations, the following steps must have been performed:
 * - Initialize the structure thanks to comedi_init_drv();
 * - At least, one subdevice descriptor has to declared into the
 *   driver thanks to comedi_add_subd();
 * 
 * Eventually, the driver must be inserted into the Comedi drivers set
 * thanks to comedi_add_drv().
 *
 * In the cleanup module function, the driver must be unregistered
 * thanks to comedi_rm_drv() and destroyed by calling
 * comedi_cleanup_drv().
 *
 * @{
 */

/**
 * @brief Add a driver to the Comedi driver list
 *
 * After initialising a driver structure, the driver must be made
 * available so as to be attached.
 *
 * @param[in] drv Driver descriptor structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_add_drv(comedi_drv_t * drv);
EXPORT_SYMBOL(comedi_add_drv);

/**
 * @brief Remove a driver from the Comedi driver list
 *
 * This function removes the driver descriptor from the Comedi driver
 * list. The driver cannot be attached anymore.
 *
 * @param[in] drv Driver descriptor structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_rm_drv(comedi_drv_t * drv);
EXPORT_SYMBOL(comedi_rm_drv);

/**
 * @brief Initialize the driver descriptor's structure
 *
 * @param[in] drv Driver descriptor structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_init_drv(comedi_drv_t * drv);
EXPORT_SYMBOL(comedi_init_drv);

/**
 * @brief Clean the driver descriptor's structure up
 *
 * @param[in] drv Driver descriptor structure
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_cleanup_drv(comedi_drv_t * drv);
EXPORT_SYMBOL(comedi_cleanup_drv);

/** @} */

/* --- Subdevice section --- */

/*!
 * @ingroup driverfacilities
 * @defgroup subdevice Subdevice management services
 *
 * Subdevice declaration in a driver
 *
 * The subdevice structure is the most complex one in the Comedi
 * driver layer. It contains some description fields to fill and some
 * callbacks to declare.
 *
 * The description fields are:
 * - flags: to define the subdevice type and its capabilities;
 * - chan_desc: to describe the channels which compose the subdevice;
 * - rng_desc: to declare the usable ranges;
 *
 * The functions callbakcs are:
 * - do_cmd() and do_cmdtest(): to performe asynchronous acquisitions
 *   thanks to commands;
 * - cancel(): to abort a working asynchronous acquisition;
 * - munge(): to apply modifications on the data freshly acquired
 *   during an asynchronous transfer. Warning: using this feature with
 *   can significantly reduce the performances (if the munge operation
 *   is complex, it will trigger high CPU charge and if the
 *   acquisition device is DMA capable, many cache-misses and
 *   cache-replaces will occur (the benefits of the DMA controller
 *   will vanish);
 * - trigger(): optionnaly to launch an asynchronous acquisition;
 * - insn_read(), insn_write(), insn_bits(), insn_config(): to perform
 *   synchronous acquisition operations.
 *
 * Once the subdevice is filled, it must be inserted into the driver
 * structure thanks to comedi_add_subd().
 *
 * @{
 */

EXPORT_SYMBOL(range_bipolar10);
EXPORT_SYMBOL(range_bipolar5);
EXPORT_SYMBOL(range_unipolar10);
EXPORT_SYMBOL(range_unipolar5);

/**
 * @brief Add a subdevice to the driver descriptor
 * 
 * Once the driver descriptor structure is initialized, the function
 * comedi_add_subd() must be used so to add some subdevices to the
 * driver.
 *
 * @param[in] drv Driver descriptor structure
 * @param[in] subd Subdevice descriptor structure
 *
 * @return the index with which the subdevice has been registered, in
 * case of error a negative error code is returned.
 *
 */
int comedi_add_subd(comedi_drv_t * drv, comedi_subd_t * subd);
EXPORT_SYMBOL(comedi_add_subd);

/**
 * @brief Get the channels count registered on a specific subdevice.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] subd_key Subdevice index
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_get_nbchan(comedi_dev_t * dev, int subd_key);
EXPORT_SYMBOL(comedi_get_nbchan);

/** @} */

/* --- Buffer section --- */

/*!
 * @ingroup driverfacilities
 * @defgroup comedi_buffer Buffer management services
 *
 * Buffer management services
 *
 * The buffer is the key component of the Comedi infrastructure. It
 * manages transfers between the user-space and the Comedi drivers
 * thanks to generic functions which are described hereafter. Thanks
 * to the buffer subsystem, the driver developer does not have to care
 * about the way the user program retrieves or sends data.
 *
 * To write a classical char driver, the developer has to fill a fops
 * structure so as to provide transfer operations to the user program
 * (read, write, ioctl and mmap if need be).
 *
 * The Comedi infrastructure manages the whole interface with the
 * userspace; the common read, write, mmap, etc. callbacks are generic
 * Comedi functions. These functions manage (and perform, if need be)
 * tranfers between the user-space and an asynchronous buffer thanks
 * to lockless mechanisms.
 *
 * Consequently, the developer has to use the proper buffer functions
 * in order to write / read acquired data into / from the asynchronous
 * buffer.
 *
 * Here are listed the functions:
 * - comedi_buf_prepare_(abs)put() and comedi_buf_commit_(abs)put()
 * - comedi_buf_prepare_(abs)get() and comedi_buf_commit_(abs)get()
 * - comedi_buf_put()
 * - comedi_buf_get()
 * - comedi_buf_evt().
 *
 * The functions count might seem high; however, the developer needs a
 * few of them to write a driver. Having so many functions enables to
 * manage any transfer cases:
 * - If some DMA controller is available, there is no need to make the
 *   driver copy the acquired data into the asynchronous buffer, the
 *   DMA controller must directly trigger DMA shots into / from the
 *   buffer. In that case, a function comedi_buf_prepare_*() must be
 *   used so as to set up the DMA transfer and a function
 *   comedi_buf_commit_*() has to be called to complete the
 *   transfer().
 * - For DMA controllers which need to work with global counter (the
 *   transfered data count since the beginning of the acquisition),
 *   the functions comedi_buf_*_abs_*() have been made available.
 * - If no DMA controller is available, the driver has to perform the
 *   copy between the hardware component and the asynchronous
 *   buffer. In such cases, the functions comedi_buf_get() and
 *   comedi_buf_put() are useful.
 *
 * @{
 */

/**
 * @brief Update the absolute count of data sent from the device to
 * the buffer since the start of the acquisition and after the next
 * DMA shot
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(absg)et() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The data count to be transferred during the next
 * DMA shot plus the data count which have been copied since the start
 * of the acquisition
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_prepare_absput(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_prepare_absput);

/**
 * @brief Set the absolute count of data which was sent from the
 * device to the buffer since the start of the acquisition and until
 * the last DMA shot
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(abs)get() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The data count transferred to the buffer during
 * the last DMA shot plus the data count which have been sent /
 * retrieved since the beginning of the acquisition
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_commit_absput(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_commit_absput);

/**
 * @brief Set the count of data which is to be sent to the buffer at
 * the next DMA shot
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(abs)get() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The data count to be transferred
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_prepare_put(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_prepare_put);

/**
 * @brief Set the count of data sent to the buffer during the last
 * completed DMA shots
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(abs)get() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The amount of data transferred
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_commit_put(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_commit_put);

/**
 * @brief Copy some data from the device driver to the buffer
 *
 * The function comedi_buf_put() must copy data coming from some
 * acquisition device to the Comedi buffer. This ring-buffer is an
 * intermediate area between the device driver and the user-space
 * program, which is supposed to recover the acquired data.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] bufdata The data buffer to copy into the Comedi buffer
 * @param[in] count The amount of data to copy
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_put(comedi_dev_t * dev, void *bufdata, unsigned long count);
EXPORT_SYMBOL(comedi_buf_put);

/**
 * @brief Update the absolute count of data sent from the buffer to
 * the device since the start of the acquisition and after the next
 * DMA shot
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(absg)et() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The data count to be transferred during the next
 * DMA shot plus the data count which have been copied since the start
 * of the acquisition
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_prepare_absget(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_prepare_absget);

/**
 * @brief Set the absolute count of data which was sent from the
 * buffer to the device since the start of the acquisition and until
 * the last DMA shot
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(abs)get() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The data count transferred to the device during
 * the last DMA shot plus the data count which have been sent since
 * the beginning of the acquisition
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_commit_absget(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_commit_absget);

/**
 * @brief Set the count of data which is to be sent from the buffer to
 * the device at the next DMA shot
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(abs)get() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The data count to be transferred
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_prepare_get(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_prepare_get);

/**
 * @brief Set the count of data sent from the buffer to the device
 * during the last completed DMA shots
 *
 * The functions comedi_buf_prepare_(abs)put(),
 * comedi_buf_commit_(abs)put(), comedi_buf_prepare_(abs)get() and
 * comedi_buf_commit_(abs)get() have been made available for DMA
 * transfers. In such situations, no data copy is needed between the
 * Comedi buffer and the device as some DMA controller is in charge of
 * performing data shots from / to the Comedi buffer. However, some
 * pointers stil have to be updated so as to monitor the tranfers.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] count The amount of data transferred
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_commit_get(comedi_dev_t * dev, unsigned long count);
EXPORT_SYMBOL(comedi_buf_commit_get);

/**
 * @brief Copy some data from the buffer to the device driver
 *
 * The function comedi_buf_get() must copy data coming from the Comedi
 * buffer to some acquisition device. This ring-buffer is an
 * intermediate area between the device driver and the user-space
 * program, which is supposed to provide the data to send to the
 * device.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] bufdata The data buffer to copy into the Comedi buffer
 * @param[in] count The amount of data to copy
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_get(comedi_dev_t * dev, void *bufdata, unsigned long count);
EXPORT_SYMBOL(comedi_buf_get);

/**
 * @brief Signal some event(s) to a uer-space program involved in some
 * read / write operation
 *
 * The function comedi_buf_evt() is useful in many cases:
 * - To wake-up a process waiting for some data to read.
 * - To wake-up a process waiting for some data to write.
 * - To notify the user-process an error has occured during the
 *   acquistion.
 *
 * @param[in] dev Device descriptor structure
 * @param[in] type Buffer transfer type:
 * - COMEDI_BUF_PUT for device -> Comedi buffer -> user-process
 *   transfer.
 * - COMEDI_BUF_GET for user-process -> Comedi_buffer -> device
 *   transfer
 * @param[in] evts Some specific event to notify:
 * - COMEDI_BUF_ERROR to indicate some error has occured during the
 *   transfer
 * - COMEDI_BUF_EOA to indicate the acquisition is complete (this
 *   event is automatically set, it should not be used).
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_buf_evt(comedi_dev_t * dev, unsigned int type, unsigned long evts);
EXPORT_SYMBOL(comedi_buf_evt);

/**
 * @brief Get the data amount available in the Comedi buffer 
 *
 * @param[in] dev Device descriptor structure
 * @param[in] type Buffer transfer type:
 * - COMEDI_BUF_PUT for device -> Comedi buffer -> user-process
 *   transfer; in this case, the returned count is the free space in
 *   the Comedi buffer in which the driver can put acquired data.
 * - COMEDI_BUF_GET for user-process -> Comedi_buffer -> device
 *   transfer; in that case, the returned count is the data amount
 *   available for sending to the device.
 *
 * @return the amount of data available in the Comedi buffer.
 *
 */
unsigned long comedi_buf_count(comedi_dev_t * dev, unsigned int type);
EXPORT_SYMBOL(comedi_buf_count);

/**
 * @brief Get the current Comedi command descriptor
 *
 * @param[in] dev Device descriptor structure
 * @param[in] type Buffer transfer type:
 * - COMEDI_BUF_PUT for device -> Comedi buffer -> user-process
 *   transfer.
 * - COMEDI_BUF_GET for user-process -> Comedi_buffer -> device
 *   transfer.
 * @param[in] idx_subd Subdevice key index; this argument is optional:
 * if the "type" is not correct, the last argument is used to select
 * the proper subdevice.
 *
 * @return the command descriptor.
 *
 */
comedi_cmd_t *comedi_get_cmd(comedi_dev_t * dev,
			     unsigned int type, int idx_subd);
EXPORT_SYMBOL(comedi_get_cmd);


/**
 * @brief Get the channel index according to its type
 *
 * @param[in] dev Device descriptor structure
 * @param[in] type Buffer transfer type:
 * - COMEDI_BUF_PUT for device -> Comedi buffer -> user-process
 *   transfer.
 * - COMEDI_BUF_GET for user-process -> Comedi_buffer -> device
 *   transfer.
 * @param[in] idx_subd Subdevice key index; this argument is optional:
 * if the "type" is not correct, the last argument is used to select
 * the proper subdevice.
 *
 * @return the channel index.
 *
 */
int comedi_get_chan(struct comedi_device *dev, unsigned int type, int idx_subd);
EXPORT_SYMBOL(comedi_get_chan);

/** @} */

/* --- IRQ handling section --- */

/*!
 * @ingroup driverfacilities
 * @defgroup comedi_interrupt Interrupt management services
 * @{
 */

/**
 * @brief Get the interrupt number in use for a specific device
 *
 * @param[in] dev Device descriptor structure
 *
 * @return the line number used or COMEDI_IRQ_UNUSED if no interrupt
 * is registered.
 *
 */
unsigned int comedi_get_irq(comedi_dev_t * dev);
EXPORT_SYMBOL(comedi_get_irq);

/**
 * @brief Register an interrupt handler for a specific device
 *
 * @param[in] dev Device descriptor structure
 * @param[in] irq Line number of the addressed IRQ
 * @param[in] handler Interrupt handler
 * @param[in] flags Registration flags:
 * - COMEDI_IRQ_SHARED: enable IRQ-sharing with other drivers
 *   (Warning: real-time drivers and non-real-time drivers cannot
 *   share an interrupt line).
 * - COMEDI_IRQ_EDGE: mark IRQ as edge-triggered (Warning: this flag
 *   is meaningless in RTDM-less context).
 * - COMEDI_IRQ_DISABLED: keep IRQ disabled when calling the action
 *   handler (Warning: this flag is ignored in RTDM-enabled
 *   configuration).
 * @param[in] cookie Pointer to be passed to the interrupt handler on
 * invocation
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_request_irq(comedi_dev_t * dev,
		       unsigned int irq,
		       comedi_irq_hdlr_t handler,
		       unsigned long flags, void *cookie);
EXPORT_SYMBOL(comedi_request_irq);

/**
 * @brief Release an interrupt handler for a specific device
 *
 * @param[in] dev Device descriptor structure
 * @param[in] irq Line number of the addressed IRQ
 *
 * @return 0 on success, otherwise negative error code.
 *
 */
int comedi_free_irq(comedi_dev_t * dev, unsigned int irq);
EXPORT_SYMBOL(comedi_free_irq);

/** @} */

/* --- Misc section --- */

/*!
 * @ingroup driverfacilities
 * @defgroup misc Misc services
 * @{
 */

#ifdef DOXYGEN_CPP		/* Only used for doxygen doc generation */

/**
 * @brief Intialise and start a Comedi task
 *
 * This function belongs to a minimal set of task management services
 * (with comedi_task_destroy() and comedi_task_sleep()). Such features
 * are not critical for Comedi driver development.
 *
 * @param[in,out] task Task handle
 * @param[in] name Optional task name
 * @param[in] proc Procedure to be executed by the task
 * @param[in] arg Custom argument passed to @c proc() on entry
 * @param[in] priority Priority of the task
 *
 * @return 0 on success, otherwise negative error code
 *
 */
int comedi_task_init(comedi_task_t * task,
		     const char *name,
		     comedi_task_proc_t proc, void *arg, int priority);

/**
 * @brief Destroy a Comedi task
 *
 * This function belongs to a minimal set of task management services
 * (with comedi_task_init() and comedi_task_sleep()). Such features
 * are not critical for Comedi driver development.
 *
 * @param[in,out] task Task handle
 *
 */
void comedi_task_destroy(comedi_task_t * task);

/**
 * @brief Make the current Comedi task passively wait a defined delay
 *
 * This function belongs to a minimal set of task management services
 * (with comedi_task_init() and comedi_task_destroy()). Such features
 * are not critical for Comedi driver development.
 *
 * @param[in] nsdelay Amount of time expressed in nanoseconds during
 * which the Comedi task must be sleeping.
 *
 * @return 0 on success, otherwise negative error code
 *
 */
int comedi_task_sleep(unsigned long long nsdelay);

#endif /* DOXYGEN_CPP */

/**
 * @brief Get the absolute time in nanoseconds
 *
 * @return the absolute time expressed in nanoseconds
 *
 */
unsigned long long comedi_get_time(void);
EXPORT_SYMBOL(comedi_get_time);

/** @} */
