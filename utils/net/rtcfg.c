/***
 *
 *  tools/rtcfg.c
 *
 *  Real-Time Configuration Distribution Protocol
 *
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <arpa/inet.h>

#include <rtcfg_chrdev.h>


#define DFLT_PACKET_SIZE        1500 /* Ethernet packet */
#define DFLT_CLIENT_BURST_RATE  4

#define MIN(a, b) ((a) < (b) ? (a) : (b))


int              f;
struct rtcfg_cmd cmd;


void help(void)
{
    fprintf(stderr, "usage (server):\n"
        "\trtcfg <dev> server [-p period] [-b burstrate] [-h <heartbeat>]\n"
        "\t      [-t <threshold>] [-r]\n"
        "\trtcfg <dev> add <address> [-hw <hw_address>] "
            "[-stage1 <stage1_file>]\n"
        "\t      [-stage2 <stage2_file>] [-t <timeout>]\n"
        "\trtcfg <dev> del <address>\n"
        "\trtcfg <dev> wait [-t <timeout>]\n"
        "\trtcfg <dev> ready [-t <timeout>]\n"
        "\trtcfg <dev> detach\n\n"
        "usage (client):\n"
        "\trtcfg <dev> client [-t <timeout>] [-c|-f <stage1_file>] "
            "[-m maxstations]\n"
        "\trtcfg <dev> announce [-t <timeout>] [-c|-f <stage2_file>]\n"
        "\t      [-b burstrate] [-r]\n"
        "\trtcfg <dev> ready [-t <timeout>]\n"
        "\trtcfg <dev> detach\n");

    exit(1);
}



int getintopt(int argc, int pos, char *argv[], int min)
{
    int result;


    if (pos >= argc)
        help();
    if ((sscanf(argv[pos], "%u", &result) != 1) || (result < min)) {
        fprintf(stderr, "invalid parameter: %s %s\n", argv[pos-1], argv[pos]);
        exit(1);
    }

    return result;
}



void cmd_server(int argc, char *argv[])
{
    int i;


    cmd.args.server.period    = 1000;
    cmd.args.server.burstrate = 4;
    cmd.args.server.heartbeat = 1000;
    cmd.args.server.threshold = 2;
    cmd.args.server.flags     = 0;

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0)
            cmd.args.server.period = getintopt(argc, ++i, argv, 1);
        else if (strcmp(argv[i], "-b") == 0)
            cmd.args.server.burstrate = getintopt(argc, ++i, argv, 1);
        else if (strcmp(argv[i], "-h") == 0)
            cmd.args.server.heartbeat = getintopt(argc, ++i, argv, 0);
        else if (strcmp(argv[i], "-t") == 0)
            cmd.args.server.threshold = getintopt(argc, ++i, argv, 1);
        else if (strcmp(argv[i], "-r") == 0)
            cmd.args.server.flags |= FLAG_READY;
        else
            help();
    }

    i = ioctl(f, RTCFG_IOC_SERVER, &cmd);
    if (i < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void cmd_add(int argc, char *argv[])
{
    int               i;
    struct in_addr    ip_addr;
    struct ether_addr mac_addr;
    const char        *stage1_filename = NULL;
    const char        *stage2_filename = NULL;
    int               file;
    size_t            buf_size;
    void              *new_buf;


    if (argc < 4)
        help();

    if (inet_aton(argv[3], &ip_addr)) {
        cmd.args.add.addr_type = RTCFG_ADDR_IP;
        cmd.args.add.ip_addr   = ip_addr.s_addr;
    } else if (ether_aton_r(argv[3], &mac_addr) != NULL) {
        cmd.args.add.addr_type = RTCFG_ADDR_MAC;
        memcpy(cmd.args.add.mac_addr, mac_addr.ether_addr_octet,
               sizeof(mac_addr.ether_addr_octet));
    } else {
        fprintf(stderr, "invalid IP or physical address: %s\n", argv[3]);
        exit(1);
    }

    cmd.args.add.stage1_data     = NULL;
    cmd.args.add.stage1_size     = 0;
    cmd.args.add.stage2_filename = NULL;
    cmd.args.add.timeout         = 0;   /* infinite */

    for (i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-hw") == 0) {
            if ((++i >= argc) || (ether_aton_r(argv[i], &mac_addr) == NULL))
                help();
            cmd.args.add.addr_type = RTCFG_ADDR_IP | FLAG_ASSIGN_ADDR_BY_MAC;
            memcpy(cmd.args.add.mac_addr, mac_addr.ether_addr_octet,
                   sizeof(mac_addr.ether_addr_octet));
        } else if (strcmp(argv[i], "-stage1") == 0) {
            if (++i >= argc)
                help();
            stage1_filename = argv[i];
        } else if (strcmp(argv[i], "-stage2") == 0) {
            if (++i >= argc)
                help();
            stage2_filename = argv[i];
        } else if (strcmp(argv[i], "-t") == 0)
            cmd.args.add.timeout = getintopt(argc, ++i, argv, 0);
        else
            help();
    }

    if (stage1_filename != NULL) {
        if (strcmp(stage1_filename, "-") == 0)
            file = 0; /* stdin */
        else {
            file = open(stage1_filename, O_RDONLY);
            if (file < 0) {
                perror("open stage 1 file");
                exit(1);
            }
        }

        buf_size = 0;
        do {
            buf_size += 4096;

            new_buf = realloc(cmd.args.add.stage1_data, buf_size);
            if (new_buf == NULL) {
                fprintf(stderr, "insufficient memory\n");
                if (cmd.args.add.stage1_data != NULL)
                    free(cmd.args.add.stage1_data);
                exit(1);
            }
            cmd.args.add.stage1_data = new_buf;

            i = read(file, cmd.args.add.stage1_data+cmd.args.add.stage1_size,
                     4096);
            if (i < 0) {
                perror("read stage 1 file");
                free(cmd.args.add.stage1_data);
                exit(1);
            }
            cmd.args.add.stage1_size += i;
        } while (i == 4096);

        close(file);
    }

    if (stage2_filename != NULL) {
        cmd.args.add.stage2_filename = malloc(PATH_MAX);
        if (cmd.args.add.stage2_filename == NULL) {
            fprintf(stderr, "insufficient memory\n");
            if (cmd.args.add.stage1_data != NULL)
                free(cmd.args.add.stage1_data);
            exit(1);
        }

        if (realpath(stage2_filename,
                     (char *)cmd.args.add.stage2_filename) == NULL) {
            perror("resolve stage 2 file");
            free((void *)cmd.args.add.stage2_filename);
            if (cmd.args.add.stage1_data != NULL)
                free(cmd.args.add.stage1_data);
            exit(1);
        }
    }

    i = ioctl(f, RTCFG_IOC_ADD, &cmd);

    if (cmd.args.add.stage1_data != NULL)
        free(cmd.args.add.stage1_data);
    if (cmd.args.add.stage2_filename != NULL)
        free((void *)cmd.args.add.stage2_filename);

    if (i < 0) {
        switch (errno) {
            case ESTAGE1SIZE:
                fprintf(stderr, "stage 1 file too big\n");
                break;

            case EEXIST:
                fprintf(stderr, "client entry already exists\n");
                break;

            default:
                perror("ioctl (add)");
        }
        exit(1);
    }
    exit(0);
}



void cmd_del(int argc, char *argv[])
{
    int                 i;
    struct in_addr      ip_addr;
    struct ether_addr   mac_addr;


    if (argc != 4)
        help();

    if (inet_aton(argv[3], &ip_addr)) {
        cmd.args.del.addr_type = RTCFG_ADDR_IP;
        cmd.args.del.ip_addr   = ip_addr.s_addr;
    } else if (ether_aton_r(argv[3], &mac_addr) != NULL) {
        cmd.args.del.addr_type = RTCFG_ADDR_MAC;
        memcpy(cmd.args.del.mac_addr, mac_addr.ether_addr_octet,
               sizeof(mac_addr.ether_addr_octet));
    } else {
        fprintf(stderr, "invalid IP or physical address: %s\n", argv[3]);
        exit(1);
    }

    i = ioctl(f, RTCFG_IOC_DEL, &cmd);
    if (i < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void cmd_wait(int argc, char *argv[])
{
    int i;


    cmd.args.wait.timeout = 0;  /* infinite */

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0)
            cmd.args.wait.timeout = getintopt(argc, ++i, argv, 0);
        else
            help();
    }

    i = ioctl(f, RTCFG_IOC_WAIT, &cmd);
    if (i < 0) {
        if (errno != ETIME)
            perror("ioctl");
        exit(1);
    }
    exit(0);
}



void cmd_client(int argc, char *argv[])
{
    int  i;
    int  cfg_size;
    int  cfg_file      = -1;
    char *cfg_filename = NULL;
    int  buffer_size   = 0;


    cmd.args.client.timeout      = 0; /* infinite */
    cmd.args.client.max_stations = 32;

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0) {
            cmd.args.client.timeout = getintopt(argc, ++i, argv, 0);
        } else if (strcmp(argv[i], "-c") == 0) {
            cfg_file    = 1; /* standard output file descriptor */
            buffer_size = DFLT_PACKET_SIZE;
        } else if (strcmp(argv[i], "-f") == 0) {
            if (++i >= argc)
                help();
            cfg_filename = argv[i];
            buffer_size  = DFLT_PACKET_SIZE;
        } else if (strcmp(argv[i], "-m") == 0)
            cmd.args.client.max_stations = getintopt(argc, ++i, argv, 1);
        else
            help();
    }

    if (buffer_size > 0) {
        cmd.args.client.buffer = malloc(buffer_size);
        if (cmd.args.client.buffer == NULL) {
            fprintf(stderr, "insufficient memory\n");
            exit(1);
        }
    }
    cmd.args.client.buffer_size = buffer_size;

    cfg_size = ioctl(f, RTCFG_IOC_CLIENT, &cmd);

    /* Buffer too small? Let's try again! */
    if (cfg_size > buffer_size) {
        free(cmd.args.client.buffer);

        buffer_size = i;
        cmd.args.client.buffer = malloc(buffer_size);
        if (cmd.args.client.buffer == NULL) {
            fprintf(stderr, "insufficient memory\n");
            exit(1);
        }
        cmd.args.client.buffer_size = buffer_size;

        cfg_size = ioctl(f, RTCFG_IOC_CLIENT, &cmd);
    }

    if (cfg_size < 0) {
        if (errno != ETIME)
            perror("ioctl");
        if (cmd.args.client.buffer_size > 0)
            free(cmd.args.client.buffer);
        exit(1);
    }

    if (cfg_filename != NULL) {
        cfg_file = open(cfg_filename, O_CREAT | O_WRONLY | O_TRUNC,
                        S_IREAD | S_IWRITE);
        if (cfg_file < 0) {
            perror("create output file");
            free(cmd.args.client.buffer);
            exit(1);
        }
    }

    if (cfg_file > 0) {
        i = write(cfg_file, cmd.args.client.buffer, cfg_size);
        free(cmd.args.client.buffer);
        if (i < 0) {
            perror("write output file");
            exit(1);
        }
    }

    exit(0);
}



void cmd_announce(int argc, char *argv[])
{
    int    i;
    int    cfg_size;
    int    cfg_file      = -1;
    char   *cfg_filename = NULL;
    size_t buffer_size   = 0;


    cmd.args.announce.timeout     = 0; /* infinite */
    cmd.args.announce.buffer_size = 0;
    cmd.args.announce.flags       = 0;
    cmd.args.announce.burstrate   = DFLT_CLIENT_BURST_RATE;

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0)
            cmd.args.announce.timeout = getintopt(argc, ++i, argv, 0);
        else if (strcmp(argv[i], "-c") == 0) {
            cfg_file    = 1; /* standard output file descriptor */
            buffer_size = DFLT_CLIENT_BURST_RATE * DFLT_PACKET_SIZE;
        } else if (strcmp(argv[i], "-f") == 0) {
            if (++i >= argc)
                help();
            cfg_filename = argv[i];
            buffer_size  = DFLT_CLIENT_BURST_RATE * DFLT_PACKET_SIZE;
        } else if (strcmp(argv[i], "-b") == 0)
            cmd.args.announce.burstrate = getintopt(argc, ++i, argv, 1);
        else if (strcmp(argv[i], "-r") == 0)
            cmd.args.announce.flags |= FLAG_READY;
        else
            help();
    }

    if (buffer_size > 0) {
        cmd.args.announce.buffer = malloc(buffer_size);
        if (cmd.args.announce.buffer == NULL) {
            fprintf(stderr, "insufficient memory\n");
            exit(1);
        }
        cmd.args.announce.flags |= FLAG_STAGE_2_DATA;
    }
    cmd.args.announce.buffer_size = buffer_size;

    if (cfg_filename != NULL) {
        cfg_file = open(cfg_filename, O_CREAT | O_WRONLY | O_TRUNC,
                        S_IREAD | S_IWRITE);
        if (cfg_file < 0) {
            perror("create output file");
            free(cmd.args.announce.buffer);
            exit(1);
        }
    }

    while ((cfg_size = ioctl(f, RTCFG_IOC_ANNOUNCE, &cmd)) > 0) {
        i = write(cfg_file, cmd.args.announce.buffer, cfg_size);
        if (i < 0) {
            perror("write output file");
            exit(1);
        }
    }

    if (cmd.args.announce.buffer != NULL)
        free(cmd.args.announce.buffer);

    if (i < 0) {
        if (errno != ETIME)
            perror("ioctl");
        exit(1);
    }
    exit(0);
}



void cmd_ready(int argc, char *argv[])
{
    int i;


    cmd.args.ready.timeout = 0; /* infinite */

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0)
            cmd.args.ready.timeout = getintopt(argc, ++i, argv, 0);
        else
            help();
    }

    i = ioctl(f, RTCFG_IOC_READY, &cmd);
    if (i < 0) {
        if (errno != ETIME)
            perror("ioctl");
        exit(1);
    }
    exit(0);
}



void cmd_detach(int argc, char *argv[])
{
    if (argc > 3)
        help();

    if (ioctl(f, RTCFG_IOC_DETACH, &cmd) < 0) {
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

    memset(&cmd, 0, sizeof(cmd));
    strncpy(cmd.head.if_name, argv[1], IFNAMSIZ);

    if (strcmp(argv[2], "server") == 0)
        cmd_server(argc, argv);
    if (strcmp(argv[2], "add") == 0)
        cmd_add(argc, argv);
    if (strcmp(argv[2], "del") == 0)
        cmd_del(argc, argv);
    if (strcmp(argv[2], "wait") == 0)
        cmd_wait(argc, argv);

    if (strcmp(argv[2], "client") == 0)
        cmd_client(argc, argv);
    if (strcmp(argv[2], "announce") == 0)
        cmd_announce(argc, argv);
    if (strcmp(argv[2], "ready") == 0)
        cmd_ready(argc, argv);

    if (strcmp(argv[2], "detach") == 0)
        cmd_detach(argc, argv);

    help();

    return 0;
}
