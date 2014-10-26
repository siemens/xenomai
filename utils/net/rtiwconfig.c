#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/ether.h>
#include <netinet/in.h>

#include <rtwlan_io.h>

#define PRINT_FLAG_ALL          1
#define PRINT_FLAG_INACTIVE     2

int f;
struct rtwlan_cmd cmd;

void help(void) {

    fprintf(stderr, "Usage:\n"
            "\trtiwconfig --help\n"
            "\trtiwconfig [<dev>]\n"
            "\trtiwconfig <dev> bitrate <2|4|11|22|12|18|24|36|48|72|96|108>\n"
            "\trtiwconfig <dev> channel <1-13>\n"
            "\rrtiwconfig <dev> retry   <0-255>\n"
            "\trtiwconfig <dev> txpower <0-100>\n"
            "\trtiwconfig <dev> bbpsens <0-127>\n"
            "\trtiwconfig <dev> mode <raw|ack|monitor>\n"
            "\trtiwconfig <dev> autoresponder <0|1>\n"
            "\trtiwconfig <dev> dropbcast <0|1>\n"
            "\trtiwconfig <dev> dropmcast <0|1>\n"
            "\t-- WARNING: Direct register access may cause system hang ! --\n"
            "\trtiwconfig <dev> regdump\n"
            "\trtiwconfig <dev> regread <offset>\n"
            "\trtiwconfig <dev> regwrite <offset> <value>\n"
            "\trtiwconfig <dev> bbpwrite <reg_id> <value>\n"
            );

    exit(1);
}

void print_dev(void) {
  
    printf("\n");
    printf("%s\n", cmd.head.if_name);
    printf("bitrate: %d\t\t", cmd.args.info.bitrate);

    printf("txpower: %d\n", cmd.args.info.txpower);
    printf("channel: %d\t\t", cmd.args.info.channel);
    printf("retry: %d\n", cmd.args.info.retry);
    printf("autoresponder: %d\t", cmd.args.info.autoresponder);
    printf("bbp sensibility: %d\n", cmd.args.info.bbpsens);
    printf("drop broadcast: %d\t", cmd.args.info.dropbcast);
    printf("rx packets: %5d\n", cmd.args.info.rx_packets);
    printf("drop multicast: %d\t", cmd.args.info.dropmcast);
    printf("tx packets: %5d\n", cmd.args.info.tx_packets);
    printf("tx mode: ");
    switch(cmd.args.info.mode) {
    case RTWLAN_TXMODE_RAW:
        printf("raw");
        break;
    case RTWLAN_TXMODE_ACK:
        printf("ack");
        break;
    case RTWLAN_TXMODE_MCAST:
        printf("mcast");
        break;
    default:
        printf("unknown");
    }
    printf("\t\ttx retry: %7d\n", cmd.args.info.tx_retry);
}

void do_display(int print_flags) {

    int i;
    int ret;
  
    if ((print_flags & PRINT_FLAG_ALL) != 0)
        for (i = 1; i <= MAX_RT_DEVICES; i++) {
            cmd.args.info.ifindex = i;

            ret = ioctl(f, IOC_RTWLAN_IFINFO, &cmd);
            if (ret == 0) {
                if (((print_flags & PRINT_FLAG_INACTIVE) != 0) ||
                    ((cmd.args.info.flags & IFF_RUNNING) != 0))
                    print_dev();
            } 
            else if (ret == -ENORTWLANDEV) {
                continue;
            }
            else if (errno != ENODEV) {
                perror("ioctl");
                exit(1);
            }
        }
    else {
        cmd.args.info.ifindex = 0;
    
        ret = ioctl(f, IOC_RTWLAN_IFINFO, &cmd);
        if(ret == -ENORTWLANDEV) {
            printf("Device %s has no wireless extensions !\n", cmd.head.if_name);
            exit(1);
        } 
        else if (ret < 0) {
            perror("ioctl");
            exit(1);
        }
    
        print_dev();
         
    }

    printf("\n");

    exit(0);
}

int main(int argc, char * argv[]) {

    int offset, ret = 0;

    if ((argc > 1) && (strcmp(argv[1], "--help") == 0))
        help();

    f = open("/dev/rtnet", O_RDWR);

    if (f < 0) {
        perror("/dev/rtnet");
        exit(1);
    }

    if(argc > 1)
        strncpy(cmd.head.if_name, argv[1], IFNAMSIZ);

    switch(argc) {
    case 1:
        do_display(PRINT_FLAG_ALL);
        break;
    case 2:
        do_display(0);
        break;
    case 3:
        if(strcmp(argv[2], "regdump") == 0) {

            for(offset=0x0; offset <= 0x0174; offset+=0x04) {
	
                cmd.args.reg.address = offset;
                ret = ioctl(f, IOC_RTWLAN_REGREAD, &cmd);
                printf("rtiwconfig: offset=%3x reg=%8x\n", cmd.args.reg.address, cmd.args.reg.value);
            }
        } else
            help();
        break;
    case 4:
        if (strcmp(argv[2], "channel") == 0) {
            cmd.args.set.channel = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_CHANNEL, &cmd);
        } 
        else if(strcmp(argv[2], "bitrate") == 0) {
            cmd.args.set.bitrate = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_BITRATE, &cmd);
        }
        else if(strcmp(argv[2], "txpower") == 0) {
            cmd.args.set.txpower = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_TXPOWER, &cmd);
        }
        else if(strcmp(argv[2], "retry") == 0) {
            cmd.args.set.retry = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_RETRY, &cmd);
        }
        else if(strcmp(argv[2], "regread") == 0) {
            sscanf(argv[3], "%x", &cmd.args.reg.address);
            ret = ioctl(f, IOC_RTWLAN_REGREAD, &cmd);
            printf("rtiwconfig: regread: address=%3x value=%8x\n", cmd.args.reg.address, cmd.args.reg.value);
        }
        else if(strcmp(argv[2], "bbpread") == 0) {
            sscanf(argv[3], "%x", &cmd.args.reg.address);
            ret = ioctl(f, IOC_RTWLAN_BBPREAD, &cmd);
            printf("rtiwconfig: bbpread: address=%3x value=%4x\n", cmd.args.reg.address, cmd.args.reg.value); 
        }
        else if(strcmp(argv[2], "dropbcast") == 0) {
            cmd.args.set.dropbcast = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_DROPBCAST, &cmd);
        }
        else if(strcmp(argv[2], "dropmcast") == 0) {
            cmd.args.set.dropmcast = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_DROPMCAST, &cmd);
        }
        else if(strcmp(argv[2], "mode") == 0) {
            if(strcmp(argv[3], "raw") == 0)
                cmd.args.set.mode = RTWLAN_TXMODE_RAW;
            else if(strcmp(argv[3], "ack") == 0)
                cmd.args.set.mode = RTWLAN_TXMODE_ACK;
            else if(strcmp(argv[3], "mcast") == 0)
                cmd.args.set.mode = RTWLAN_TXMODE_MCAST;
            ret = ioctl(f, IOC_RTWLAN_TXMODE, &cmd);
        }
        else if(strcmp(argv[2], "bbpsens") == 0) {
            cmd.args.set.bbpsens = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_BBPSENS, &cmd);
        }
        else if(strcmp(argv[2], "autoresponder") == 0) {
            cmd.args.set.autoresponder = atoi(argv[3]);
            ret = ioctl(f, IOC_RTWLAN_AUTORESP, &cmd);
        }
        else
            help();
        break;
    case 5:
        if(strcmp(argv[2], "regwrite") == 0) {
            sscanf(argv[3], "%x", &cmd.args.reg.address);
            printf("regwrite: address=%x\n", cmd.args.reg.address);
            sscanf(argv[4], "%x", &cmd.args.reg.value);
            printf("regwrite: value=%x\n", cmd.args.reg.value);
            ret = ioctl(f, IOC_RTWLAN_REGWRITE, &cmd);
        }
        else if(strcmp(argv[2], "bbpwrite") == 0) {
            sscanf(argv[3], "%x", &cmd.args.reg.address);
            sscanf(argv[4], "%x", &cmd.args.reg.value);
            ret = ioctl(f, IOC_RTWLAN_BBPWRITE, &cmd);
        }
        break;
    default:
        help();
    }

    if(ret) {
        perror("ioctl");
        exit(1);
    }
  
    return ret;
}
