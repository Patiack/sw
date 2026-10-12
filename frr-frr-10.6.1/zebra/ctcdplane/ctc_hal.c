#include <zebra.h>
#include <fcntl.h>
#include "lib/log.h"
#include "ctc_hal.h"
#include "../../../../hal/hal_msg.h"

hal_sock_t hal_netlink = {-1, NETLINK_RUNOS, 0, {0}, "hal-cmd"};
hal_sock_t hal_netlink_async = {-1, NETLINK_RUNOS_ASYNC, 0, {0}, "hal-listen"};

#define VS_NETLINK_CMD_BUFFER_SIZE  2048

typedef struct hsl_cmd_s {
	struct nlmsghdr n;
	char buf[VS_NETLINK_CMD_BUFFER_SIZE];
} hal_cmd_t;

int g_hsl_fd = 0;
int g_try_open_frr = 0;

int hal_cmd_send(int module, int cmd, void *data, int data_len, void *obuf,
		 int *plen)
{
	int ret;
	hsl_msg_t msg;

	memset(&msg, 0, sizeof(msg));
	msg.module = module;
	msg.cmd = cmd;
	msg.output = obuf;
	if (plen) {
		msg.output_size = *plen;
	} else {
		msg.output_size = 0;
	}

	msg.input = data;
	msg.input_len = data_len;
	zlog_info("CTC-DPLANE: g_hsl_fd = %d", g_hsl_fd);
	if ((g_hsl_fd == -1) && (!g_try_open_frr)) {
		hal_init(HAL_FRR);
		g_try_open_frr = 1;
	}
	if (g_hsl_fd == -1) {
		return -1;
	}

	ret = ioctl(g_hsl_fd, 1, &msg);

	if (ret == 0) {
		if (obuf && plen) {
			*plen = msg.output_len;
		}
		ret = msg.ret;
	} else {
		if (obuf && plen) {
			*plen = 0;
		}
	}

	return ret;
}

static int hal_socket_init(struct hal_sock *nl, unsigned int module)
{
	int ret;
	struct sockaddr_nl snl;
	int sock;
	int namelen;

	sock = socket(AF_NETLINK, SOCK_RAW, nl->protocol);
	if (sock < 0) {
		zlog_info("CTC-DPLANE: hal_socket_init socket return -1");
		return -1;
	}

	memset(&snl, 0, sizeof snl);
	snl.nl_family = AF_NETLINK;
	snl.nl_groups = 0;
	snl.nl_pid = module;

	/* Bind the socket to the netlink structure for anything. */
	ret = bind(sock, (struct sockaddr *)&snl, sizeof snl);

	if (ret < 0) {
		close(sock);
		zlog_info("CTC-DPLANE: hal_socket_init bind return -1");
		return -1;
	}

	/* multiple netlink sockets will have different nl_pid */
	namelen = sizeof(snl);
	ret = getsockname(sock, (struct sockaddr *)&snl,
			  (socklen_t *)&namelen);
	if (ret < 0 || namelen != sizeof(snl)) {
		close(sock);
		zlog_info("CTC-DPLANE: hal_socket_init getsockname return -1");
		return -1;
	}

	nl->snl = snl;
	nl->sock = sock;

	return ret;
}

void hal_init(unsigned int module)
{
	//hal_socket_init(&hal_netlink, module);
	//hal_socket_init(&hal_netlink_async, module);

	g_hsl_fd = open("/dev/hsl_cdev_fd_frr", O_RDWR);
}