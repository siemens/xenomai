#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <sys/mman.h>

#include <native/task.h>
#include <native/pipe.h>

#include <rtdm/rtcan.h>

static void print_usage(char *prg)
{
    fprintf(stderr,
	    "Usage: %s <can-interface> [Options]\n"
	    "Options:\n"
	    " -f  --filter=id:mask[:id:mask]... apply filter\n"
	    " -e  --error=mask      receive error messages\n"
	    " -t, --timeout=MS      timeout in ms\n"
	    " -v, --verbose         be verbose\n"
	    " -p, --print=MODULO    print every MODULO message\n"
	    " -n, --name=STRING     name of the RT task\n"
	    " -h, --help            this help\n",
	    prg);
}


extern int optind, opterr, optopt;

static int s = -1, running = 1, verbose = 0, print = 1;
static nanosecs_rel_t timeout = 0;

RT_TASK rt_task_desc;

#define BUF_SIZ	255
#define MAX_FILTER 16

struct sockaddr_can recv_addr;
struct can_filter recv_filter[MAX_FILTER];
static int filter_count = 0;

int add_filter(u_int32_t id, u_int32_t mask)
{
    if (filter_count >= MAX_FILTER)
	return -1;
    recv_filter[filter_count].can_id = id;
    recv_filter[filter_count].can_mask = mask;
    printf("Filter #%d: id=0x%08x mask=0x%08x\n", filter_count, id, mask);
    filter_count++;
    return 0;
}

void cleanup(void)
{
    int ret;

    if (verbose)
	printf("Cleaning up...\n");

    usleep(100000);
    if (s >= 0) {
	ret = rt_dev_close(s);
	s = -1;
	if (ret) {
	    fprintf(stderr, "rt_dev_close: %s\n", strerror(-ret));
	}
	rt_task_delete(&rt_task_desc);
    }
    }

void cleanup_and_exit(int sig)
{
    if (verbose)
	printf("Signal %d received\n", sig);
    running = 0;
    cleanup();
    exit(0);
}

void rt_task(void *arg)
{
    int i, ret, count = 0;
    struct can_frame frame;

    while (running) {
	ret = rt_dev_recv(s, (void *)&frame, sizeof(can_frame_t), 0);
	if (ret < 0) {
	    switch (ret) {
	    case -ETIMEDOUT:
		if (verbose)
		    printf("rt_dev_recv: timed out");
		continue;
	    case -EBADF:
		if (verbose)
		    printf("rt_dev_recv: aborted because socket was closed");
		break;
	    default:
		fprintf(stderr, "rt_dev_recv: %s\n", strerror(-ret));
	    }
	    running = 0;
	    break;
	}

	if (print && (count % print) == 0) {
	    printf("#%d: ", count);
	    if (frame.can_id & CAN_ERR_FLAG)
		printf("!0x%08x!", frame.can_id & CAN_ERR_MASK);
	    else if (frame.can_id & CAN_EFF_FLAG)
		printf("<0x%08x>", frame.can_id & CAN_EFF_MASK);
	    else
		printf("<0x%03x>", frame.can_id & CAN_SFF_MASK);

	    printf(" [%d]", frame.can_dlc);
	    for (i = 0; i < frame.can_dlc; i++) {
		printf(" %02x", frame.data[i]);
	    }
	    if (frame.can_id & CAN_ERR_FLAG) {
		printf(" ERROR ");
		if (frame.can_id & CAN_ERR_BUSOFF)
		    printf("bus-off");
		if (frame.can_id & CAN_ERR_CRTL)
		    printf("controller problem");
	    } else if (frame.can_id & CAN_RTR_FLAG)
		printf(" remote request");
	    printf("\n");
	}
	count++;
    }
}

int main(int argc, char **argv)
{
    int opt, ret;
    u_int32_t id, mask;
    u_int32_t err_mask = 0;
    struct ifreq ifr;
    char *ptr;

    struct option long_options[] = {
	{ "help", no_argument, 0, 'h' },
	{ "verbose", no_argument, 0, 'v'},
	{ "filter", required_argument, 0, 'f'},
	{ "error", required_argument, 0, 'e'},
	{ "timeout", required_argument, 0, 't'},
	{ 0, 0, 0, 0},
    };

    mlockall(MCL_CURRENT | MCL_FUTURE);

    signal(SIGTERM, cleanup_and_exit);
    signal(SIGINT, cleanup_and_exit);

    while ((opt = getopt_long(argc, argv, "hve:f:t:p:n:",
			      long_options, NULL)) != -1) {
	switch (opt) {
	case 'h':
	    print_usage(argv[0]);
	    exit(0);

	case 'p':
	    print = strtoul(optarg, NULL, 0);

	case 'v':
	    verbose = 1;
	    break;

	case 'e':
	    err_mask = strtoul(optarg, NULL, 0);
	    break;

	case 'f':
	    ptr = optarg;
	    while (1) {
		id = strtoul(ptr, NULL, 0);
		ptr = strchr(ptr, ':');
		if (!ptr) {
		    fprintf(stderr, "filter must be applied in the form id:mask[:id:mask]...\n");
		    exit(1);
		}
		ptr++;
		mask = strtoul(ptr, NULL, 0);
		ptr = strchr(ptr, ':');
		add_filter(id, mask);
		if (!ptr)
		    break;
		ptr++;
	    }
	    break;

	case 't':
	    timeout = (nanosecs_rel_t)strtoul(optarg, NULL, 0) * 1000000;
	    break;

	default:
	    fprintf(stderr, "Unknown option %c\n", opt);
	    break;
	}
    }

    if (optind == argc) {
	print_usage(argv[0]);
	exit(0);
    }

    if (argv[optind] == NULL) {
	fprintf(stderr, "No Interface supplied\n");
	exit(-1);
    }

    if (verbose)
	printf("interface %s\n", argv[optind]);

    ret = rt_dev_socket(PF_CAN, SOCK_RAW, 0);
    if (ret < 0) {
	fprintf(stderr, "rt_dev_socket: %s\n", strerror(-ret));
	return -1;
    }
    s = ret;

    strncpy(ifr.ifr_name, argv[optind], IFNAMSIZ);
    if (verbose)
	printf("s=%d, ifr_name=%s\n", s, ifr.ifr_name);

    ret = rt_dev_ioctl(s, SIOCGIFINDEX, &ifr);
    if (ret < 0) {
	fprintf(stderr, "rt_dev_ioctl GET_IFINDEX: %s\n", strerror(-ret));
	goto failure;
    }


    if (err_mask) {
	ret = rt_dev_setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
				&err_mask, sizeof(err_mask));
	if (ret < 0) {
	    fprintf(stderr, "rt_dev_setsockopt: %s\n", strerror(-ret));
	    goto failure;
	}
	if (verbose)
	    printf("Using err_mask=%#x\n", err_mask);
    }

    if (filter_count) {
	ret = rt_dev_setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER,
				&recv_filter, filter_count *
				sizeof(struct can_filter));
	if (ret < 0) {
	    fprintf(stderr, "rt_dev_setsockopt: %s\n", strerror(-ret));
	    goto failure;
	}
    }

    recv_addr.can_family = AF_CAN;
    recv_addr.can_ifindex = ifr.ifr_ifindex;
    ret = rt_dev_bind(s, (struct sockaddr *)&recv_addr,
		      sizeof(struct sockaddr_can));
    if (ret < 0) {
	fprintf(stderr, "rt_dev_bind: %s\n", strerror(-ret));
	goto failure;
    }

    if (timeout) {
	if (verbose)
	    printf("Timeout: %lld ns\n", timeout);
	ret = rt_dev_ioctl(s, RTCAN_RTIOC_RCV_TIMEOUT, &timeout);
	if (ret) {
	    fprintf(stderr, "rt_dev_ioctl RCV_TIMEOUT: %s\n", strerror(-ret));
	    goto failure;
	}
    }

    rt_timer_set_mode(TM_ONESHOT);

    ret = rt_task_spawn(&rt_task_desc, "", 0, 99, 0, &rt_task, 0);
    if (ret) {
        fprintf(stderr, "rt_task_spawn: %s\n", strerror(-ret));
	goto failure;
    }

    while (running)
	usleep(100000);

    cleanup();
    return 0;

 failure:
    cleanup();
    return -1;
}
