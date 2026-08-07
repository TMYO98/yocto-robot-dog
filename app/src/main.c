// SPDX-License-Identifier: MIT
/*
 * robot-core-controller
 *
 * Minimal SocketCAN control daemon for the Raspberry Pi + CAN (FD) HAT.
 *  - opens one or more CAN interfaces (classic CAN or CAN FD, auto-detected via MTU)
 *  - receives frames with epoll and dispatches them to handle_frame()
 *  - sends a periodic heartbeat (ID 0x700 + node_id) on every interface
 *  - reports CAN error frames (bus-off, error-passive, no-ACK, ...)
 *  - exits cleanly on SIGINT/SIGTERM (systemd stop)
 *
 * Put your robot logic in handle_frame() and on_tick().
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>

#define MAX_PORTS 4
#define HEARTBEAT_BASE_ID 0x700u

struct can_port {
	char name[IFNAMSIZ];
	int fd;
	bool fd_capable; /* interface MTU is CANFD_MTU */
	uint64_t rx, tx, tx_err, bus_err;
};

static struct {
	struct can_port ports[MAX_PORTS];
	int nports;
	unsigned node_id;
	unsigned period_ms;
	bool verbose;
	uint32_t tick;
} g = { .node_id = 1, .period_ms = 100 };

/* ------------------------------------------------------------------------- */

static int port_open(struct can_port *p)
{
	struct ifreq ifr = { 0 };
	struct sockaddr_can addr = { 0 };
	int one = 1;
	can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT | CAN_ERR_CRTL | CAN_ERR_PROT |
				  CAN_ERR_TRX | CAN_ERR_ACK | CAN_ERR_BUSOFF |
				  CAN_ERR_BUSERROR | CAN_ERR_RESTARTED;

	p->fd = socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
	if (p->fd < 0) {
		perror("socket(PF_CAN)");
		return -1;
	}

	snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", p->name);
	if (ioctl(p->fd, SIOCGIFINDEX, &ifr) < 0) {
		fprintf(stderr, "%s: interface not found: %s\n", p->name, strerror(errno));
		goto fail;
	}
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (ioctl(p->fd, SIOCGIFMTU, &ifr) == 0 && ifr.ifr_mtu == CANFD_MTU) {
		if (setsockopt(p->fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &one, sizeof(one)) == 0)
			p->fd_capable = true;
	}

	setsockopt(p->fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask));

	if (bind(p->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "%s: bind: %s\n", p->name, strerror(errno));
		goto fail;
	}

	printf("%s: opened (%s)\n", p->name, p->fd_capable ? "CAN FD" : "classic CAN");
	return 0;
fail:
	close(p->fd);
	p->fd = -1;
	return -1;
}

static void port_send(struct can_port *p, const struct canfd_frame *f, bool fd_frame)
{
	size_t len = fd_frame ? CANFD_MTU : CAN_MTU;
	ssize_t n = write(p->fd, f, len);

	if (n == (ssize_t)len) {
		p->tx++;
		return;
	}
	/* ENOBUFS/EAGAIN: TX queue full (typically no other node ACKing).
	 * ENETDOWN: interface is down. Rate-limit the log so we don't flood journald. */
	if (p->tx_err++ % 100 == 0)
		fprintf(stderr, "%s: tx failed (%s), %llu failures so far\n", p->name,
			n < 0 ? strerror(errno) : "short write",
			(unsigned long long)p->tx_err);
}

/* ------------------------------------------------------------------------- */
/* Application hooks — put the robot logic here.                             */

static void handle_frame(struct can_port *p, const struct canfd_frame *f, bool fd_frame)
{
	if (g.verbose) {
		printf("%s: rx %s id=0x%03X len=%u data=", p->name, fd_frame ? "FD " : "CAN",
		       f->can_id & CAN_EFF_MASK, f->len);
		for (unsigned i = 0; i < f->len; i++)
			printf("%02X", f->data[i]);
		printf("\n");
	}

	/* Example: respond to a "ping" on 0x600 + node_id by echoing the payload
	 * back on 0x580 + node_id. Replace with your own protocol. */
	if ((f->can_id & CAN_EFF_FLAG) == 0 && (f->can_id & CAN_SFF_MASK) == 0x600u + g.node_id) {
		struct canfd_frame reply = *f;
		reply.can_id = 0x580u + g.node_id;
		port_send(p, &reply, fd_frame);
	}
}

static void on_tick(void)
{
	struct canfd_frame hb = { 0 };

	g.tick++;
	hb.can_id = HEARTBEAT_BASE_ID + g.node_id;
	hb.len = 8;
	hb.data[0] = 0x05; /* state: operational */
	memcpy(&hb.data[4], &g.tick, sizeof(g.tick)); /* little-endian counter */

	for (int i = 0; i < g.nports; i++)
		port_send(&g.ports[i], &hb, false); /* classic frame: every node can read it */

	/* Every ~10 s print stats. */
	if (g.tick % (10000 / g.period_ms ? 10000 / g.period_ms : 1) == 0) {
		for (int i = 0; i < g.nports; i++) {
			struct can_port *p = &g.ports[i];
			printf("%s: rx=%llu tx=%llu tx_err=%llu bus_err=%llu\n", p->name,
			       (unsigned long long)p->rx, (unsigned long long)p->tx,
			       (unsigned long long)p->tx_err, (unsigned long long)p->bus_err);
		}
	}
}

/* ------------------------------------------------------------------------- */

static void handle_error_frame(struct can_port *p, const struct canfd_frame *f)
{
	p->bus_err++;
	if (f->can_id & CAN_ERR_BUSOFF)
		fprintf(stderr, "%s: BUS-OFF\n", p->name);
	if (f->can_id & CAN_ERR_RESTARTED)
		fprintf(stderr, "%s: controller restarted\n", p->name);
	if (f->can_id & CAN_ERR_ACK && g.verbose)
		fprintf(stderr, "%s: no ACK (is another node / termination present?)\n", p->name);
	if (f->can_id & CAN_ERR_CRTL) {
		if (f->data[1] & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE))
			fprintf(stderr, "%s: error-passive\n", p->name);
		if (f->data[1] & (CAN_ERR_CRTL_RX_OVERFLOW | CAN_ERR_CRTL_TX_OVERFLOW))
			fprintf(stderr, "%s: controller buffer overflow\n", p->name);
	}
}

static void port_read(struct can_port *p)
{
	struct canfd_frame f;

	for (;;) {
		ssize_t n = read(p->fd, &f, sizeof(f));
		if (n < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				fprintf(stderr, "%s: read: %s\n", p->name, strerror(errno));
			return;
		}
		if (n != CAN_MTU && n != CANFD_MTU)
			continue;
		if (f.can_id & CAN_ERR_FLAG) {
			handle_error_frame(p, &f);
			continue;
		}
		p->rx++;
		handle_frame(p, &f, n == CANFD_MTU);
	}
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [-i IFACE]... [-n NODE_ID] [-p PERIOD_MS] [-v]\n"
		"  -i IFACE      CAN interface (repeatable, default can0, max %d)\n"
		"  -n NODE_ID    node id 1..127 (default 1)\n"
		"  -p PERIOD_MS  heartbeat/control period in ms (default 100)\n"
		"  -v            log every received frame\n",
		argv0, MAX_PORTS);
}

int main(int argc, char **argv)
{
	int opt, ep, tfd, sfd, rc = EXIT_SUCCESS;
	sigset_t mask;

	while ((opt = getopt(argc, argv, "i:n:p:vh")) != -1) {
		switch (opt) {
		case 'i':
			if (g.nports >= MAX_PORTS) {
				fprintf(stderr, "too many interfaces\n");
				return EXIT_FAILURE;
			}
			snprintf(g.ports[g.nports++].name, IFNAMSIZ, "%s", optarg);
			break;
		case 'n':
			g.node_id = (unsigned)strtoul(optarg, NULL, 0);
			break;
		case 'p':
			g.period_ms = (unsigned)strtoul(optarg, NULL, 0);
			break;
		case 'v':
			g.verbose = true;
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}
	if (g.nports == 0)
		snprintf(g.ports[g.nports++].name, IFNAMSIZ, "can0");
	if (g.node_id < 1 || g.node_id > 127 || g.period_ms == 0) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	/* Line-buffer stdout so journald gets lines promptly. */
	setvbuf(stdout, NULL, _IOLBF, 0);

	ep = epoll_create1(EPOLL_CLOEXEC);
	if (ep < 0) {
		perror("epoll_create1");
		return EXIT_FAILURE;
	}

	for (int i = 0; i < g.nports; i++) {
		struct epoll_event ev = { .events = EPOLLIN, .data.ptr = &g.ports[i] };
		if (port_open(&g.ports[i]) < 0)
			return EXIT_FAILURE; /* systemd will restart us */
		epoll_ctl(ep, EPOLL_CTL_ADD, g.ports[i].fd, &ev);
	}

	/* Periodic timer. */
	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	struct itimerspec its = {
		.it_interval = { g.period_ms / 1000, (g.period_ms % 1000) * 1000000L },
		.it_value = { g.period_ms / 1000, (g.period_ms % 1000) * 1000000L },
	};
	timerfd_settime(tfd, 0, &its, NULL);
	struct epoll_event tev = { .events = EPOLLIN, .data.ptr = &tfd };
	epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &tev);

	/* Signals via signalfd so shutdown happens in the main loop. */
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
	struct epoll_event sev = { .events = EPOLLIN, .data.ptr = &sfd };
	epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &sev);

	printf("robot-core-controller: node %u, period %u ms, %d interface(s)\n", g.node_id,
	       g.period_ms, g.nports);

	for (bool running = true; running;) {
		struct epoll_event evs[MAX_PORTS + 2];
		int n = epoll_wait(ep, evs, MAX_PORTS + 2, -1);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("epoll_wait");
			rc = EXIT_FAILURE;
			break;
		}
		for (int i = 0; i < n; i++) {
			void *src = evs[i].data.ptr;
			if (src == &tfd) {
				uint64_t expirations;
				if (read(tfd, &expirations, sizeof(expirations)) > 0)
					on_tick();
			} else if (src == &sfd) {
				struct signalfd_siginfo si;
				if (read(sfd, &si, sizeof(si)) > 0)
					printf("caught signal %u, shutting down\n", si.ssi_signo);
				running = false;
			} else {
				port_read((struct can_port *)src);
			}
		}
	}

	for (int i = 0; i < g.nports; i++)
		close(g.ports[i].fd);
	close(tfd);
	close(sfd);
	close(ep);
	return rc;
}
