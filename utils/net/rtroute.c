/***
 *
 *  tools/rtroute.c
 *  manages IP host and network routes for RTnet
 *
 *  rtnet - real-time networking subsystem
 *  Copyright (C) 2004 Jan Kiszka <jan.kiszka@web.de>
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <arpa/inet.h>

#include <ipv4_chrdev.h>


int             f;
struct ipv4_cmd cmd;
struct in_addr  addr;


/* help gcc a bit... */
void help(void) __attribute__((noreturn));

void help(void)
{
    fprintf(stderr, "Usage:\n"
        "\trtroute\n"
        "\trtroute solicit <addr> dev <dev>\n"
        "\trtroute add <addr> <hwaddr> dev <dev>\n"
        "\trtroute add <addr> netmask <mask> gw <gw-addr>\n"
        "\trtroute del <addr> [dev <dev>]\n"
        "\trtroute del <addr> netmask <mask>\n"
        "\trtroute get <addr> [dev <dev>]\n"
        "\trtroute -f <host-routes-file>\n"
        );

    exit(1);
}



void print_routes(void)
{
    char        buf[4096];
    int         proc;
    size_t      size;
    const char  host_route[] = "/proc/rtnet/ipv4/host_route";
    const char  net_route[]  = "/proc/rtnet/ipv4/net_route";


    if ((proc = open(host_route, O_RDONLY)) < 0) {
        perror(host_route);
        exit(1);
    }

    printf("Host Routing Table\n");
    while ((size = read(proc, buf, sizeof(buf))) > 0)
        write(STDOUT_FILENO, buf, size);

    close(f);

    if ((proc = open(net_route, O_RDONLY)) < 0) {
        if (errno == ENOENT) {
            /* Network routing is not available */
            exit(0);
        }
        perror(net_route);
        exit(1);
    }

    printf("\nNetwork Routing Table\n");
    while ((size = read(proc, buf, sizeof(buf))) > 0)
        write(STDOUT_FILENO, buf, size);

    close(f);

    exit(0);
}



void route_solicit(int argc, char *argv[])
{
    int ret;


    if ((argc != 5) || (strcmp(argv[3], "dev") != 0))
        help();

    strncpy(cmd.head.if_name, argv[4], IFNAMSIZ);
    cmd.args.solicit.ip_addr = addr.s_addr;

    ret = ioctl(f, IOC_RT_HOST_ROUTE_SOLICIT, &cmd);
    if (ret < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void route_add(int argc, char *argv[])
{
    struct ether_addr   dev_addr;
    int                 ret;


    if (argc == 6) {
        /*** add host route ***/
        if ((ether_aton_r(argv[3], &dev_addr) == NULL) ||
            (strcmp(argv[4], "dev") != 0))
            help();

        cmd.args.addhost.ip_addr = addr.s_addr;
        memcpy(cmd.args.addhost.dev_addr, dev_addr.ether_addr_octet,
               sizeof(dev_addr.ether_addr_octet));
        strncpy(cmd.head.if_name, argv[5], IFNAMSIZ);

        ret = ioctl(f, IOC_RT_HOST_ROUTE_ADD, &cmd);
    } else if (argc == 7) {
        /*** add network route ***/
        if ((strcmp(argv[3], "netmask") != 0) || (strcmp(argv[5], "gw") != 0))
            help();

        cmd.args.addnet.net_addr = addr.s_addr;
        if (!inet_aton(argv[4], &addr))
            help();
        cmd.args.addnet.net_mask = addr.s_addr;
        if (!inet_aton(argv[6], &addr))
            help();
        cmd.args.addnet.gw_addr = addr.s_addr;

        ret = ioctl(f, IOC_RT_NET_ROUTE_ADD, &cmd);
    } else
        help();

    if (ret < 0) {
        perror("ioctl");
        exit(1);
    }

    exit(0);
}



void invalid_line_format(int line, char *file)
{
    fprintf(stderr, "error on line %u of file %s, expected file format:\n"
            "# comment\n"
            "<addr> <hwaddr> <dev>\n"
            "...\n", line, file);
}



void route_listadd(char *name)
{
    FILE                *fp;
    int                 line = 0;
    int                 argc = 0;
    int                 ret;
    struct ether_addr   dev_addr;
    char                buf[100];
    char                *sp;
    char                *args[4];
    const char          space[] = " \t";


    /*** try to open file ***/
    fp = fopen(name, "r");
    if (!fp) {
        ret = errno;
        fprintf(stderr, "opening file %s", name);
        perror(NULL);
        exit(1);
    }

    /*** fill buffer from file and add route ***/
    while (fgets(buf, sizeof(buf), fp)) {
        line++;

        /* find newline char and make it end of string */
        sp = strchr(buf, '\n');
        if (sp)
            *sp = '\0';

        /* ignore comments and empty lines */
        if ((buf[0] == '#') || (buf[0] == '\0'))
            continue;

        /* split string into tokens */
        argc = 0;
        args[argc] = strtok(buf, space);
        do {
            if (++argc > 3)
                break;
            args[argc] = strtok(NULL, space);
        } while (args[argc]);

        /* wrong number of arguments? */
        if (argc != 3) {
            invalid_line_format(line, name);
            continue;
        }

        /*** check data ***/

        /* check MAC */
        if (ether_aton_r(args[1], &dev_addr) == NULL) {
            invalid_line_format(line, name);
            continue;
        }

        /* check IP */
        if (!inet_aton(args[0], &addr)) {
            invalid_line_format(line, name);
            continue;
        }

        /*** turn it all into a cmd for rtnet and execute ***/
        cmd.args.addhost.ip_addr = addr.s_addr;
        memcpy(cmd.args.addhost.dev_addr, dev_addr.ether_addr_octet,
               sizeof(dev_addr.ether_addr_octet));

        /* use device <dev> */
        strncpy(cmd.head.if_name, args[2], IFNAMSIZ);

        ret = ioctl(f, IOC_RT_HOST_ROUTE_ADD, &cmd);

        if (ret < 0) {
            perror("ioctl");
            exit(1);
        }

    }
    fclose(fp);

    exit(0);
}



void route_delete(int argc, char *argv[])
{
    int ret;


    if (argc == 3) {
        /*** delete host route ***/
        cmd.args.delhost.ip_addr = addr.s_addr;

        ret = ioctl(f, IOC_RT_HOST_ROUTE_DELETE, &cmd);
    } else if (argc == 5) {
        /*** delete device specific route ***/
        if (strcmp(argv[3], "dev") == 0) {
            cmd.args.delhost.ip_addr = addr.s_addr;
            strncpy(cmd.head.if_name, argv[4], IFNAMSIZ);
            ret = ioctl(f, IOC_RT_HOST_ROUTE_DELETE_DEV, &cmd);
        }
        /*** delete network route ***/
        else if (strcmp(argv[3], "netmask") == 0) {
            cmd.args.delnet.net_addr = addr.s_addr;
            if (!inet_aton(argv[4], &addr))
                help();
            cmd.args.delnet.net_mask = addr.s_addr;

            ret = ioctl(f, IOC_RT_NET_ROUTE_DELETE, &cmd);
        }
        else
            help();
    } else
        help();

    if (ret < 0) {
        if (errno == ENOENT)
            fprintf(stderr, "Specified route not found\n");
        else
            perror("ioctl");
        exit(1);
    }

    exit(0);
}



void route_get(int argc, char *argv[])
{
    int ret;


    if (argc == 3) {
        /*** get host route ***/
        cmd.args.gethost.ip_addr = addr.s_addr;

        ret = ioctl(f, IOC_RT_HOST_ROUTE_GET, &cmd);
    } else if (argc == 5) {
        /*** get device specific route ***/
        if (strcmp(argv[3], "dev") == 0) {
            cmd.args.delhost.ip_addr = addr.s_addr;
            strncpy(cmd.head.if_name, argv[4], IFNAMSIZ);
            ret = ioctl(f, IOC_RT_HOST_ROUTE_GET_DEV, &cmd);
        }
        else
            help();
    } else
        help();

    if (ret >= 0) {
        unsigned char *p = cmd.args.gethost.dev_addr;
        printf("Destination\tHW Address\t\tDevice\n"
               "%s\t%02x:%02x:%02x:%02x:%02x:%02x\t%s\n", argv[2],
               p[0], p[1], p[2] , p[3], p[4], p[5], cmd.head.if_name);
    } else {
        if (errno == ENOENT) {
            fprintf(stderr, "No route for host %s", argv[2]);
            if (argc == 5)
                fprintf(stderr, "on device %s", argv[4]);
            fprintf(stderr, " found\n");
        } else
            perror("ioctl");
        exit(1);
    }

    exit(0);
}



int main(int argc, char *argv[])
{
    const char  rtnet_dev[] = "/dev/rtnet";


    if (argc == 1)
        print_routes();

    if ((strcmp(argv[1], "--help") == 0) || (argc < 3))
        help();

    f = open(rtnet_dev, O_RDWR);
    if (f < 0) {
        perror(rtnet_dev);
        exit(1);
    }

    /* add host routes from file? */
    if (strcmp(argv[1], "-f") == 0)
        route_listadd(argv[2]);

    /* second argument is now always an IP address */
    if (!inet_aton(argv[2], &addr))
        help();

    if (strcmp(argv[1], "solicit") == 0)
        route_solicit(argc, argv);
    if (strcmp(argv[1], "add") == 0)
        route_add(argc, argv);
    if (strcmp(argv[1], "del") == 0)
        route_delete(argc, argv);
    if (strcmp(argv[1], "get") == 0)
        route_get(argc, argv);

    help();

    return 0;
}
