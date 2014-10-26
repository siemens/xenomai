/***
 *
 *  rtmac/nomac/nomac_dev.c
 *
 *  RTmac - real-time networking media access control subsystem
 *  Copyright (C) 2002      Marc Kleine-Budde <kleine-budde@gmx.de>
 *                2003-2005 Jan Kiszka <Jan.Kiszka@web.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/list.h>

#include <rtdev.h>
#include <rtmac.h>
#include <rtmac/nomac/nomac.h>


static int nomac_dev_openclose(void)
{
    return 0;
}



static int nomac_dev_ioctl(struct rtdm_dev_context *context,
                           rtdm_user_info_t *user_info,
                           unsigned int request, void *arg)
{
    struct nomac_priv   *nomac;


    nomac = container_of((struct rtdm_device *)context->device,
                         struct nomac_priv, api_device);

    switch (request) {
        case RTMAC_RTIOC_TIMEOFFSET:

        case RTMAC_RTIOC_WAITONCYCLE:

        default:
            return -ENOTTY;
    }
}



int nomac_dev_init(struct rtnet_device *rtdev, struct nomac_priv *nomac)
{
    char    *pos;


    nomac->api_device.struct_version = RTDM_DEVICE_STRUCT_VER;

    nomac->api_device.device_flags = RTDM_NAMED_DEVICE;
    nomac->api_device.context_size = 0;

    strcpy(nomac->api_device.device_name, "NOMAC");
    for (pos = rtdev->name + strlen(rtdev->name) - 1;
        (pos >= rtdev->name) && ((*pos) >= '0') && (*pos <= '9'); pos--);
    strncat(nomac->api_device.device_name+5, pos+1, IFNAMSIZ-5);

    nomac->api_device.open_nrt = (rtdm_open_handler_t)nomac_dev_openclose;

    nomac->api_device.ops.close_nrt =
            (rtdm_close_handler_t)nomac_dev_openclose;

    nomac->api_device.ops.ioctl_rt  = nomac_dev_ioctl;
    nomac->api_device.ops.ioctl_nrt = nomac_dev_ioctl;

    nomac->api_device.proc_name = nomac->api_device.device_name;

    nomac->api_device.device_class     = RTDM_CLASS_RTMAC;
    nomac->api_device.device_sub_class = RTDM_SUBCLASS_UNMANAGED;
    nomac->api_device.driver_name      = "nomac";
    nomac->api_device.driver_version   = RTNET_RTDM_VER;
    nomac->api_device.peripheral_name  = "NoMAC API";
    nomac->api_device.provider_name    = rtnet_rtdm_provider_name;

    return rtdm_dev_register(&nomac->api_device);
}
