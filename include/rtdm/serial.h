/**
 * @file
 * Real-Time Driver Model for Xenomai, serial device profile header
 *
 * @note Copyright (C) 2005-2007 Jan Kiszka <jan.kiszka@web.de>
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
 *
 * @ingroup rtserial
 *
 * @ingroup profiles
 * @defgroup rtserial Serial Devices
 *
 * This is the common interface a RTDM-compliant serial device has to provide.
 * Feel free to comment on this profile via the Xenomai mailing list
 * (Xenomai-core@gna.org) or directly to the author (jan.kiszka@web.de).
 *
 * @b Profile @b Revision: 3
 * @n
 * @n
 * @par Device Characteristics
 * @ref rtdm_device.device_flags "Device Flags": @c RTDM_NAMED_DEVICE, @c RTDM_EXCLUSIVE @n
 * @n
 * @ref rtdm_device.device_name "Device Name": @c "rtser<N>", N >= 0 @n
 * @n
 * @ref rtdm_device.device_class "Device Class": @c RTDM_CLASS_SERIAL @n
 * @n
 *
 * @par Supported Operations
 * @b Open @n
 * Environments: non-RT (RT optional, deprecated)@n
 * Specific return values: none @n
 * @n
 * @b Close @n
 * Environments: non-RT (RT optional, deprecated)@n
 * Specific return values: none @n
 * @n
 * @b IOCTL @n
 * Mandatory Environments: see @ref SERIOCTLs "below" @n
 * Specific return values: see @ref SERIOCTLs "below" @n
 * @n
 * @b Read @n
 * Environments: RT (non-RT optional)@n
 * Specific return values:
 * - -ETIMEDOUT
 * - -EINTR (interrupted explicitly or by signal)
 * - -EAGAIN (no data available in non-blocking mode)
 * - -EBADF (device has been closed while reading)
 * - -EIO (hardware error or broken bit stream)
 * .
 * @n
 * @b Write @n
 * Environments: RT (non-RT optional)@n
 * Specific return values:
 * - -ETIMEDOUT
 * - -EINTR (interrupted explicitly or by signal)
 * - -EAGAIN (no data written in non-blocking mode)
 * - -EBADF (device has been closed while writing)
 */
#ifndef _RTDM_SERIAL_H
#define _RTDM_SERIAL_H

#include <rtdm/rtdm.h>
#include <rtdm/uapi/serial.h>

#endif /* !_RTDM_SERIAL_H */
