/***
 *
 *  tools/tdmacfg.c
 *  Configuration tool for the RTmac/TDMA discipline
 *
 *  RTmac - real-time networking media access control subsystem
 *  Copyright (C) 2002      Marc Kleine-Budde <kleine-budde@gmx.de>,
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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <tdma_chrdev.h>


static int                  f;
static struct tdma_config   tdma_cfg;


static void help(void)
{
    fprintf(stderr, "Usage:\n"
        "\ttdmacfg <dev> master <cycle_period> [-b <backup_offset>]\n"
        "\t        [-c calibration_rounds] [-i max_slot_id]\n"
        "\t        [-m max_calibration_requests]\n"
        "\ttdmacfg <dev> slave [-c calibration_rounds] [-i max_slot_id]\n"
        "\ttdmacfg <dev> slot <id> [<offset> [-p <phasing>/<period>] "
            "[-s <size>]\n"
        "\t         [-j <joint_slot_id>] [-l calibration_log_file]\n"
        "\t         [-t calibration_timeout]]\n"
        "\ttdmacfg <dev> detach\n");

    exit(1);
}



int getintopt(int argc, int pos, char *argv[], int min)
{
    int result;


    if (pos >= argc)
        help();
    if ((sscanf(argv[pos], "%i", &result) != 1) || (result < min)) {
        fprintf(stderr, "invalid parameter: %s %s\n", argv[pos-1], argv[pos]);
        exit(1);
    }

    return result;
}



void write_calibration_log(char *log_filename, unsigned int rounds,
                           __u64 *cal_results)
{
    char    str_buf[32];
    int     log_file;
    int     i;
    int     r;


    log_file = open(log_filename, O_CREAT | O_WRONLY | O_TRUNC,
                    S_IREAD | S_IWRITE);
    if (log_file < 0) {
        perror("create output file");
        free(cal_results);
        exit(1);
    }

    for (i = rounds-1; i >= 0; i--) {
        r = sprintf(str_buf, "%llu\n", (unsigned long long)cal_results[i]);
        if (write(log_file, str_buf, r) < 0) {
            perror("write output file");
            free(cal_results);
            exit(1);
        }
    }

    close(log_file);
    free(cal_results);
}



void do_master(int argc, char *argv[])
{
    int     r;
    int     i;


    if (argc < 4)
        help();

    if ((sscanf(argv[3], "%u", &r) != 1) || (r <= 0)) {
        fprintf(stderr, "invalid cycle period: %s\n", argv[3]);
        exit(1);
    }
    tdma_cfg.args.master.cycle_period = ((uint64_t)r) * 1000;

    tdma_cfg.args.master.backup_sync_offset = 0;
    tdma_cfg.args.master.cal_rounds         = 100;
    tdma_cfg.args.master.max_cal_requests   = 64;
    tdma_cfg.args.master.max_slot_id        = 7;

    for (i = 4; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0)
            tdma_cfg.args.master.backup_sync_offset =
                getintopt(argc, ++i, argv, 0) * 1000;
        else if (strcmp(argv[i], "-c") == 0)
            tdma_cfg.args.master.cal_rounds = getintopt(argc, ++i, argv, 0);
        else if (strcmp(argv[i], "-i") == 0)
            tdma_cfg.args.master.max_slot_id = getintopt(argc, ++i, argv, 0);
        else if (strcmp(argv[i], "-m") == 0)
            tdma_cfg.args.master.max_cal_requests =
                getintopt(argc, ++i, argv, 1);
        else
            help();
    }

    r = ioctl(f, TDMA_IOC_MASTER, &tdma_cfg);
    if (r < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void do_slave(int argc, char *argv[])
{
    int     i;
    int     r;


    if (argc < 3)
        help();

    tdma_cfg.args.slave.cal_rounds  = 100;
    tdma_cfg.args.slave.max_slot_id = 7;

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0)
            tdma_cfg.args.slave.cal_rounds = getintopt(argc, ++i, argv, 0);
        else if (strcmp(argv[i], "-i") == 0)
            tdma_cfg.args.slave.max_slot_id = getintopt(argc, ++i, argv, 0);
        else
            help();
    }

    r = ioctl(f, TDMA_IOC_SLAVE, &tdma_cfg);
    if (r < 0) {
        perror("ioctl");
        exit(1);
    }
    exit(0);
}



void do_slot(int argc, char *argv[])
{
    char            *log_filename = NULL;
    int             result_size = 0;
    unsigned int    ioc;
    int             r;
    int             i;


    if (argc < 4)
        help();

    if ((sscanf(argv[3], "%u", &r) != 1) || (r < 0)) {
        fprintf(stderr, "invalid slot id: %s\n", argv[3]);
        exit(1);
    }

    if (argc > 4) {
        tdma_cfg.args.set_slot.id = r;

        if ((sscanf(argv[4], "%u", &r) != 1) || (r < 0)) {
            fprintf(stderr, "invalid slot offset: %s\n", argv[4]);
            exit(1);
        }
        tdma_cfg.args.set_slot.offset = ((uint64_t)r) * 1000;

        tdma_cfg.args.set_slot.period      = 1;
        tdma_cfg.args.set_slot.phasing     = 0;
        tdma_cfg.args.set_slot.size        = 0;
        tdma_cfg.args.set_slot.cal_timeout = 0;
        tdma_cfg.args.set_slot.joint_slot  = -1;
        tdma_cfg.args.set_slot.cal_results = NULL;

        for (i = 5; i < argc; i++) {
            if (strcmp(argv[i], "-l") == 0) {
                if (++i >= argc)
                    help();
                log_filename = argv[i];
            } else if (strcmp(argv[i], "-p") == 0) {
                if (++i >= argc)
                    help();
                if ((sscanf(argv[i], "%u/%u",
                            &tdma_cfg.args.set_slot.phasing,
                            &tdma_cfg.args.set_slot.period) != 2) ||
                    (tdma_cfg.args.set_slot.phasing < 1) ||
                    (tdma_cfg.args.set_slot.period < 1) ||
                    (tdma_cfg.args.set_slot.phasing >
                        tdma_cfg.args.set_slot.period)) {
                    fprintf(stderr, "invalid parameter: %s %s\n", argv[i-1],
                            argv[i]);
                    exit(1);
                }
                tdma_cfg.args.set_slot.phasing--;
            } else if (strcmp(argv[i], "-s") == 0)
                tdma_cfg.args.set_slot.size =
                    getintopt(argc, ++i, argv, MIN_SLOT_SIZE);
            else if (strcmp(argv[i], "-t") == 0)
                tdma_cfg.args.set_slot.cal_timeout =
                    getintopt(argc, ++i, argv, 0);
            else if (strcmp(argv[i], "-j") == 0)
                tdma_cfg.args.set_slot.joint_slot =
                    getintopt(argc, ++i, argv, 0);
            else
                help();
        }

        if (log_filename) {
            /* note: we can reuse tdma_cfg here as the head is the same and
             *       will remain unmodified */
            result_size = ioctl(f, TDMA_IOC_CAL_RESULT_SIZE, &tdma_cfg);
            if (result_size > 0) {
                tdma_cfg.args.set_slot.cal_results =
                    (__u64 *)malloc(result_size * sizeof(__u64));
                if (!tdma_cfg.args.set_slot.cal_results) {
                    fprintf(stderr, "insufficient memory\n");
                    exit(1);
                }
            } else
                log_filename = NULL;
        }

        ioc = TDMA_IOC_SET_SLOT;
    } else {
        tdma_cfg.args.remove_slot.id = r;

        ioc = TDMA_IOC_REMOVE_SLOT;
    }

    r = ioctl(f, ioc, &tdma_cfg);
    if (r < 0) {
        perror("ioctl");
        exit(1);
    }

    if (log_filename)
        write_calibration_log(log_filename, result_size,
                              tdma_cfg.args.set_slot.cal_results);
    exit(0);
}



void do_detach(int argc, char *argv[])
{
    int r;


    if (argc != 3)
        help();

    r = ioctl(f, TDMA_IOC_DETACH, &tdma_cfg);
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

    strncpy(tdma_cfg.head.if_name, argv[1], IFNAMSIZ);

    if (strcmp(argv[2], "master") == 0)
        do_master(argc, argv);
    if (strcmp(argv[2], "slave") == 0)
        do_slave(argc, argv);
    if (strcmp(argv[2], "slot") == 0)
        do_slot(argc, argv);
    if (strcmp(argv[2], "detach") == 0)
        do_detach(argc, argv);

    help();

    return 0;
}
