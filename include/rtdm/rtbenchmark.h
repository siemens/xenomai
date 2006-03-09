/**
 * @file
 * Real-Time Driver Model for Xenomai, benchmark device profile header
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
 * @ingroup rtbenchmark
 */

/*!
 * @ingroup profiles
 * @defgroup rtbenchmark Benchmark Devices
 *
 * This group of devices is intended to provide in-kernel benchmark results.
 * Feel free to comment on this profile via the Xenomai mailing list
 * (Xenomai-help@gna.org) or directly to the author (jan.kiszka@web.de). @n
 * @n
 *
 * @par Device Characteristics
 * @ref rtdm_device.device_flags "Device Flags": @c RTDM_NAMED_DEVICE, @c RTDM_EXCLUSIVE @n
 * @n
 * @ref rtdm_device.device_name "Device Name": @c "rtbenchmark<N>", N >= 0 @n
 * @n
 * @ref rtdm_device.device_class "Device Class": @c RTDM_CLASS_BENCHMARK @n
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
 *
 * @{
 */

#ifndef _RTBENCHMARK_H
#define _RTBENCHMARK_H

#include <rtdm/rtdm.h>

#define RTBNCH_TIMER_TASK       0
#define RTBNCH_TIMER_HANDLER    1

typedef struct rtbnch_result {
    long                    avg;
    long                    min;
    long                    max;
    long                    overruns;
    long                    test_loops;
} rtbnch_result_t;

typedef struct rtbnch_timerconfig {
    int                     mode;
    uint64_t                period;
    int                     warmup_loops;
    int                     histogram_size;
    int                     histogram_bucketsize;
    int                     freeze_max;
} rtbnch_timerconfig_t;

typedef struct rtbnch_interm_result {
    struct rtbnch_result    last;
    struct rtbnch_result    overall;
} rtbnch_interm_result_t;

typedef struct rtbnch_overall_result {
    struct rtbnch_result    result;
    long                    *histogram_avg;
    long                    *histogram_min;
    long                    *histogram_max;
} rtbnch_overall_result_t;

typedef struct rtbnch_trace_special {
    unsigned char           id;
    long                    v;
} rtbnch_trace_special_t;


#define RTIOC_TYPE_BENCHMARK        RTDM_CLASS_BENCHMARK


/*!
 * @name Sub-Classes of RTDM_CLASS_BENCHMARK
 * @{ */
#define RTDM_SUBCLASS_TIMER         0
/** @} */


/*!
 * @anchor IOCTLs @name IOCTLs
 * Benchmark device IOCTLs
 * @{ */
#define RTBNCH_RTIOC_INTERM_RESULT      \
    _IOWR(RTIOC_TYPE_BENCHMARK, 0x00, struct rtbnch_interm_result)

#define RTBNCH_RTIOC_START_TMTEST       \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x10, struct rtbnch_timerconfig)

#define RTBNCH_RTIOC_STOP_TMTEST        \
    _IOWR(RTIOC_TYPE_BENCHMARK, 0x11, struct rtbnch_overall_result)

#define RTBNCH_RTIOC_BEGIN_TRACE        \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x20, long)

#define RTBNCH_RTIOC_END_TRACE          \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x21, long)

#define RTBNCH_RTIOC_FREEZE_TRACE       \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x22, long)

#define RTBNCH_RTIOC_REFREEZE_TRACE     \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x23, long)

#define RTBNCH_RTIOC_SPECIAL_TRACE      \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x24, unsigned char)

#define RTBNCH_RTIOC_SPECIAL_TRACE_EX   \
    _IOW(RTIOC_TYPE_BENCHMARK, 0x25, struct rtbnch_trace_special)
/** @} */

/** @} */

#endif /* _RTBENCHMARK_H */
