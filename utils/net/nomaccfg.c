/***
 *
 *  tools/nomaccfg.c
 *  Configuration tool for the RTmac/NoMAC discipline
 *
 *  RTmac - real-time networking media access control subsystem
 *  Copyright (C) 2002       Marc Kleine-Budde <kleine-budde@gmx.de>,
 *                2003, 2004 Jan Kiszka <Jan.Kiszka@web.de>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <nomac_chrdev.h>


static int                     f;
static struct nomac_config     nomac_cfg;


void help(void)
{
    fprintf(stderr, "Usage:\n"
        "\tnomaccfg <dev> attach\n"
        "\tnomaccfg <dev> detach\n");

    exit(1);
}



void do_attach(int argc, char *argv[])
{
    int r;


    if (argc != 3)
        help();

    r = ioctl(f, NOMAC_IOC_ATTACH, &nomac_cfg);
    if (r < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void do_detach(int argc, char *argv[])
{
    int r;


    if (argc != 3)
        help();

    r = ioctl(f, NOMAC_IOC_DETACH, &nomac_cfg);
    if (r < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



int main(int argc, char *argv[])
{
    if ((argc < 3) || (strcmp(argv[1], "--help") == 0))
        help();

    f = open("/dev/rtnet", O_RDWR);

    if (f < 0) {
        perror("/dev/rtnet");
        exit(1);
    }

    strncpy(nomac_cfg.head.if_name, argv[1], IFNAMSIZ);

    if (strcmp(argv[2], "attach") == 0)
        do_attach(argc,argv);
    if (strcmp(argv[2], "detach") == 0)
        do_detach(argc,argv);

    help();

    return 0;
}
