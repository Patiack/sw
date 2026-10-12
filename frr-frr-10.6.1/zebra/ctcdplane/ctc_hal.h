#ifndef CTC_HAL_H
#define CTC_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int ros_pid_t;

#include <linux/netlink.h>

#define NETLINK_RUNOS        25
#define NETLINK_RUNOS_ASYNC  26

/* Socket interface to kernel */
typedef struct hal_sock {
	int sock;
	int protocol;
	int seq;
	struct sockaddr_nl snl;
	const char *name;
} hal_sock_t;

#define HSL_CMD_GEN(module, cmd)  (((module) << 8) | (cmd))

extern hal_sock_t hal_netlink_async;
extern hal_sock_t hal_netlink;

extern int hal_cmd_send(int module, int cmd, void *data, int data_len,
			void *obuf, int *plen);
//extern int hal_recv(unsigned int (*filter)(unsigned int type, void *buf, unsigned int len));
//extern int hal_send_pkt(void *msg);
extern void hal_init(unsigned int module);

#ifdef __cplusplus
}
#endif

#endif