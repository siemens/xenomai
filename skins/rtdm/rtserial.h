/**
 * @file
 * Real-Time Driver Model for Xenomai, serial device profile header
 *
 * @note Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>
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
 */

/*!
 * @ingroup profiles
 * @defgroup rtserial Serial Devices
 *
 * This is a @b preliminary version of the common interface a RTDM-compliant
 * serial device has to provide. This revision may still change until the
 * final version. E.g., all definitions need to be reviewed if they do not
 * contain too much 16550A-specifics or if significant features are missing.
 * Feel free to comment on this profile via the Xenomai mailing list
 * (Xenomai-help@gna.org) or directly to the author (jan.kiszka@web.de). @n
 * @n
 *
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
 * Environments: non-RT (RT optional)@n
 * Specific return values: none @n
 * @n
 * @b Close @n
 * Environments: non-RT (RT optional)@n
 * Specific return values: none @n
 * @n
 * @b IOCTL @n
 * Mandatory Environments: see @ref IOCTLs "below" @n
 * Specific return values: see @ref IOCTLs "below" @n
 * @n
 * @b Read @n
 * Environments: RT (non-RT optional)@n
 * Specific return values:
 * - -ETIMEDOUT
 * - -EINTR (interrupted explicitly or by signal)
 * - -EAGAIN (no data available in non-blocking mode)
 * - -EBADF (device has been closed while reading)
 * .
 * @n
 * @b Write @n
 * Environments: RT (non-RT optional)@n
 * Specific return values:
 * - -ETIMEDOUT
 * - -EINTR (interrupted explicitly or by signal)
 * - -EAGAIN (no data written in non-blocking mode)
 * - -EBADF (device has been closed while writing)
 *
 * @{
 */

#ifndef _RTSERIAL_H
#define _RTSERIAL_H

#include <asm/types.h>
#include <rtdm/rtdm.h>

/*!
 * @anchor RTSER_xxx_BAUD   @name RTSER_xxx_BAUD
 * Baud rates
 * @{ */
#define RTSER_50_BAUD               2304
#define RTSER_75_BAUD               1536
#define RTSER_110_BAUD              1047
#define RTSER_134_5_BAUD            857
#define RTSER_150_BAUD              768
#define RTSER_300_BAUD              384
#define RTSER_600_BAUD              192
#define RTSER_1200_BAUD             96
#define RTSER_2400_BAUD             48
#define RTSER_3600_BAUD             32
#define RTSER_4800_BAUD             24
#define RTSER_7200_BAUD             16
#define RTSER_9600_BAUD             12
#define RTSER_19200_BAUD            6
#define RTSER_38400_BAUD            3
#define RTSER_57600_BAUD            2
#define RTSER_115200_BAUD           1
#define RTSER_DEF_BAUD              RTSER_9600_BAUD

/** Generate customised baud rate code
 * @param base UART clock base
 * @param rate baud rate
 */
#define RTSER_CUSTOM_BAUD(base, rate) \
    ((base + (rate >> 1)) / rate)
/** @} */

/*!
 * @anchor RTSER_xxx_PARITY   @name RTSER_xxx_PARITY
 * Number of parity bits
 * @{ */
#define RTSER_NO_PARITY             0x00
#define RTSER_ODD_PARITY            0x01
#define RTSER_EVEN_PARITY           0x03
#define RTSER_DEF_PARITY            RTSER_NO_PARITY
/** @} */

/*!
 * @anchor RTSER_xxx_BITS   @name RTSER_xxx_BITS
 * Number of data bits
 * @{ */
#define RTSER_5_BITS                0x00
#define RTSER_6_BITS                0x01
#define RTSER_7_BITS                0x02
#define RTSER_8_BITS                0x03
#define RTSER_DEF_BITS              RTSER_8_BITS
/** @} */

/*!
 * @anchor RTSER_xxx_STOPB   @name RTSER_xxx_STOPB
 * Number of stop bits
 * @{ */
#define RTSER_1_STOPB               0x00
/** valid only in combination with 5 data bits */
#define RTSER_1_5_STOPB             0x01
#define RTSER_2_STOPB               0x01
#define RTSER_DEF_STOPB             RTSER_1_STOPB
/** @} */

/*!
 * @anchor RTSER_xxx_HAND   @name RTSER_xxx_HAND
 * Handshake mechanisms
 * @{ */
#define RTSER_NO_HAND               0x00
#define RTSER_RTSCTS_HAND           0x01
#define RTSER_DEF_HAND              RTSER_NO_HAND
/** @} */

/*!
 * @anchor RTSER_FIFO_xxx   @name RTSER_FIFO_xxx
 * Reception FIFO interrupt threshold
 * @{ */
#define RTSER_FIFO_DEPTH_1          0x00
#define RTSER_FIFO_DEPTH_4          0x40
#define RTSER_FIFO_DEPTH_8          0x80
#define RTSER_FIFO_DEPTH_14         0xC0
#define RTSER_DEF_FIFO_DEPTH        RTSER_FIFO_DEPTH_1
/** @} */

/*!
 * @anchor RTSER_TIMEOUT_xxx   @name RTSER_TIMEOUT_xxx
 * Special timeout values
 * @{ */
#define RTSER_TIMEOUT_INFINITE      0
#define RTSER_TIMEOUT_NONE          (-1)
#define RTSER_DEF_TIMEOUT           RTSER_TIMEOUT_INFINITE
/** @} */

/*!
 * @anchor RTSER_xxx_TIMESTAMP_HISTORY   @name RTSER_xxx_TIMESTAMP_HISTORY
 * Timestamp history control
 * @{ */
#define RTSER_RX_TIMESTAMP_HISTORY  0x01
#define RTSER_DEF_TIMESTAMP_HISTORY 0x00
/** @} */

/*!
 * @anchor RTSER_EVENT_xxx   @name RTSER_EVENT_xxx
 * Events bits
 * @{ */
#define RTSER_EVENT_RXPEND          0x01
#define RTSER_EVENT_ERRPEND         0x02
#define RTSER_EVENT_MODEMHI         0x04
#define RTSER_EVENT_MODEMLO         0x08
#define RTSER_DEF_EVENT_MASK        0x00
/** @} */


/*!
 * @anchor RTSER_SET_xxx   @name RTSER_SET_xxx
 * Configuration mask bits
 * @{ */
#define RTSER_SET_BAUD              0x0001
#define RTSER_SET_PARITY            0x0002
#define RTSER_SET_DATA_BITS         0x0004
#define RTSER_SET_STOP_BITS         0x0008
#define RTSER_SET_HANDSHAKE         0x0010
#define RTSER_SET_FIFO_DEPTH        0x0020
#define RTSER_SET_TIMEOUT_RX        0x0100
#define RTSER_SET_TIMEOUT_TX        0x0200
#define RTSER_SET_TIMEOUT_EVENT     0x0400
#define RTSER_SET_TIMESTAMP_HISTORY 0x0800
#define RTSER_SET_EVENT_MASK        0x1000
/** @} */


/*!
 * @anchor RTSER_LSR_xxx   @name RTSER_LSR_xxx
 * Line status bits
 * @{ */
#define RTSER_LSR_DATA              0x01
#define RTSER_LSR_OVERRUN_ERR       0x02
#define RTSER_LSR_PARITY_ERR        0x04
#define RTSER_LSR_FRAMING_ERR       0x08
#define RTSER_LSR_BREAK_IND         0x10
#define RTSER_LSR_THR_EMTPY         0x20
#define RTSER_LSR_TRANSM_EMPTY      0x40
#define RTSER_LSR_FIFO_ERR          0x80
#define RTSER_SOFT_OVERRUN_ERR      0x0100
/** @} */


/*!
 * @anchor RTSER_MSR_xxx   @name RTSER_MSR_xxx
 * Modem status bits
 * @{ */
#define RTSER_MSR_DCTS              0x01
#define RTSER_MSR_DDSR              0x02
#define RTSER_MSR_TERI              0x04
#define RTSER_MSR_DDCD              0x08
#define RTSER_MSR_CTS               0x10
#define RTSER_MSR_DSR               0x20
#define RTSER_MSR_RI                0x40
#define RTSER_MSR_DCD               0x80
/** @} */


/*!
 * @anchor RTSER_MCR_xxx   @name RTSER_MCR_xxx
 * Modem control bits
 * @{ */
#define RTSER_MCR_DTR               0x01
#define RTSER_MCR_RTS               0x02
#define RTSER_MCR_OUT1              0x04
#define RTSER_MCR_OUT2              0x08
#define RTSER_MCR_LOOP              0x10
/** @} */


/**
 * Serial device configuration
 */
typedef struct rtser_config {
    int     config_mask;        /**< mask specifying valid fields,
                                 *   see @ref RTSER_SET_xxx */
    int     baud_rate;          /**< baud rate, see @ref RTSER_xxx_BAUD */
    int     parity;             /**< number of parity bits, see
                                 *   @ref RTSER_xxx_PARITY */
    int     data_bits;          /**< number of data bits, see
                                 *   @ref RTSER_xxx_BITS */
    int     stop_bits;          /**< number of stop bits, see
                                 *   @ref RTSER_xxx_STOPB */
    int     handshake;          /**< handshake mechanisms, see
                                 *   @ref RTSER_xxx_HAND */
    int     fifo_depth;         /**< reception FIFO interrupt threshold, see
                                 *   @ref RTSER_FIFO_xxx */
    __s64   rx_timeout;         /**< reception timeout in ns, see
                                 *   @ref RTSER_TIMEOUT_xxx for special
                                 *   values */
    __s64   tx_timeout;         /**< transmission timeout in ns, see
                                 *   @ref RTSER_TIMEOUT_xxx for special
                                 *   values */
    __s64   event_timeout;      /**< event timeout in ns, see
                                 *   @ref RTSER_TIMEOUT_xxx for special
                                 *   values */
    int     timestamp_history;  /**< enable timestamp history, see
                                 *   @ref RTSER_xxx_TIMESTAMP_HISTORY */
    int     event_mask;         /**< event mask to be used with
                                 *   @ref RTSER_RTIOC_WAIT_EVENT, see
                                 *   @ref RTSER_EVENT_xxx */
} rtser_config_t;

/**
 * Serial device status
 */
typedef struct rtser_status {
    int     line_status;    /**< line status register, see
                             *   @ref RTSER_LSR_xxx */
    int     modem_status;   /**< modem status register, see
                             *   @ref RTSER_MSR_xxx */
} rtser_status_t;

/**
 * Additional information about serial device events
 */
typedef struct rtser_event {
    int     events;             /**< signalled events, see
                                 *   @ref RTSER_EVENT_xxx */
    int     rx_pending;         /**< number of pending input characters */
    __u64   last_timestamp;     /**< last interrupt timestamp (absolute time
                                 *   in ns) */
    __u64   rxpend_timestamp;   /**< reception timestamp (absolute time in ns)
                                 *   of oldest character in input queue */
} rtser_event_t;


#define RTIOC_TYPE_SERIAL           RTDM_CLASS_SERIAL


/*!
 * @name Sub-Classes of RTDM_CLASS_SERIAL
 * @{ */
#define RTDM_SUBCLASS_16550A        0
/** @} */


/*!
 * @anchor IOCTLs @name IOCTLs
 * Serial device IOCTLs
 * @{ */

/**
 * Get serial device configuration
 *
 * @param[out] arg Pointer to configuration buffer (struct rtser_config)
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
#define RTSER_RTIOC_GET_CONFIG      \
    _IOR(RTIOC_TYPE_SERIAL, 0x00, struct rtser_config)

/**
 * Set serial device configuration
 *
 * @param[in] arg Pointer to configuration buffer (struct rtser_config)
 *
 * @return 0 on success, otherwise:
 *
 * - -EPERM is returned if the caller's context is invalid, see note below.
 *
 * - -ENOMEM is returned if a new history buffer for timestamps cannot be
 * allocated.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * @note If rtser_config contains a valid timestamp_history and the
 * addressed device has been opened in non-real-time context, this IOCTL must
 * be issued in non-real-time context as well. Otherwise, this command will
 * fail.
 *
 * Rescheduling: never.
 */
#define RTSER_RTIOC_SET_CONFIG      \
    _IOW(RTIOC_TYPE_SERIAL, 0x01, struct rtser_config)

/**
 * Get serial device status
 *
 * @param[out] arg Pointer to status buffer (struct rtser_status)
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
#define RTSER_RTIOC_GET_STATUS      \
    _IOR(RTIOC_TYPE_SERIAL, 0x02, struct rtser_status)

/**
 * Get serial device's modem contol register
 *
 * @param[out] arg Pointer to variable receiving the content (int, see
 *             @ref RTSER_MCR_xxx)
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
#define RTSER_RTIOC_GET_CONTROL     \
    _IOR(RTIOC_TYPE_SERIAL, 0x03, int)

/**
 * Set serial device's modem contol register
 *
 * @param[in] arg New control register content (int, see @ref RTSER_MCR_xxx)
 *
 * @return 0 on success, otherwise negative error code
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task (RT, non-RT)
 *
 * Rescheduling: never.
 */
#define RTSER_RTIOC_SET_CONTROL     \
    _IOW(RTIOC_TYPE_SERIAL, 0x04, int)

/**
 * Wait on serial device events according to previously set mask
 *
 * @param[out] arg Pointer to event information buffer (struct rtser_event)
 *
 * @return 0 on success, otherwise:
 *
 * - -EBUSY is returned if another task is already waiting on events of this
 * device.
 *
 * - -EBADF is returned if the file descriptor is invalid or the device has
 * just been closed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (RT)
 *
 * Rescheduling: possible.
 */
#define RTSER_RTIOC_WAIT_EVENT      \
    _IOR(RTIOC_TYPE_SERIAL, 0x05, struct rtser_event)
/** @} */

/** @} */

#endif /* _RTSERIAL_H */
