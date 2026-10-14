// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Process id output.
 * Copyright (C) 1998, 1999 Kunihiro Ishiguro
 */

#include <zebra.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <log.h>
#include "lib/version.h"
#include "network.h"
#include "lib_errors.h"

/* ============================================================================
 * 【文件导读】pid_output.c —— 写 pid 文件，并用文件锁做“单实例”保护
 * ============================================================================
 * 【它到底解决什么问题】守护进程启动时要写 /var/run/xxx.pid。难点不在“写”，
 * 而在**防止同一个守护进程被启动两份**：两个实例都写同一个 pid 文件会互相覆盖，
 * 排查起来极其迷惑。本文件用 POSIX 记录锁(fcntl F_SETLK / F_WRLCK)解决：同一
 * pid 文件同时只允许一个进程持有写锁，后来的进程锁不上就判定“已有实例在跑”。
 *
 * 【锁的三个属性，决定了它的行为】
 *   ① 记录锁是**按进程**而非按 fd 的：进程退出(含崩溃)时内核会自动释放，
 *      所以不会出现“上次崩溃留下的僵尸锁把服务卡死”。
 *   ② 用的是 F_SETLK(非阻塞)而不是 F_SETLKW(阻塞)：拿不到锁立刻失败退出，
 *      而不是挂在这里等——守护进程绝不能因为别人占着文件就卡住启动。
 *   ③ 锁保护的是“谁在跑”，文件内容(pid 值)只是**方便人来读**的，二者不可混淆。
 *
 * 【umask 那一段为什么存在】pid 文件应固定为 0644，不受启动者 umask 影响，
 * 于是先 umask() 收窄再 open()，无论成败都立刻恢复旧 umask。注意恢复动作在
 * **两条分支里各写了一次**，这不是冗余，是要覆盖 open 失败提前 return 的路径。
 *
 * 【阅读顺序】umask 与 open(建立文件并收窄权限) → set_cloexec(不把 fd 泄漏给
 * 子进程) → fcntl 加锁(单实例判定的核心) → 写入 pid 并 ftruncate(缩掉旧内容)。
 * ============================================================================
 */
#define PIDFILE_MASK 0644

/* 【函数】写 pid 文件并持有其写锁；失败即 exit(1)。
 *   path: pid 文件路径。
 *   返回: 当前的 pid(getpid())。**注意正常路径下本函数几乎必然成功**——任何
 *         失败(建不了文件、锁不上、写不进)都是直接 exit(1)，不返回错误码。
 *         所以调用方拿到返回值就代表“我已是唯一实例且 pid 文件已写好”。
 * 【为什么失败要 exit 而不是返回错误】守护进程缺了 pid 文件也没有继续运行的
 *   意义(尤其“锁不上”通常意味着**另一个实例正在跑**)，早退出更利于运维定位。
 * 【易踩的坑】
 *   ① 锁的是**文件**而不是路径：若有人删除 pid 文件再重建，新文件上的锁与旧
 *      实例无关，单实例保护会失效——这是记录锁方案的固有局限，别指望它防删。
 *   ② 写失败时只记日志而**不退出**，函数仍返回 pid；这是有意保留的宽松处理。
 *   ③ 写入长度用 strlen(buf)(含末尾 '\n')，随后 ftruncate 到同一长度以清掉
 *      上次运行遗留的更长内容——若省略 ftruncate，读到的是 "12345\n6" 这种
 *      混合内容。 */
pid_t pid_output(const char *path)
{
	int tmp;
	int fd;
	pid_t pid;
	char buf[16];
	struct flock lock;
	mode_t oldumask;

	pid = getpid();

	oldumask = umask(0777 & ~PIDFILE_MASK);
	fd = open(path, O_RDWR | O_CREAT, PIDFILE_MASK);
	if (fd < 0) {
		flog_err_sys(EC_LIB_SYSTEM_CALL,
			     "Can't create pid lock file %s (%s), exiting",
			     path, safe_strerror(errno));
		umask(oldumask);
		exit(1);
	} else {
		size_t pidsize;

		umask(oldumask);
		memset(&lock, 0, sizeof(lock));

		set_cloexec(fd);

		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;

		if (fcntl(fd, F_SETLK, &lock) < 0) {
			flog_err_sys(EC_LIB_SYSTEM_CALL,
				     "Could not lock pid_file %s (%s), exiting.  Please ensure that the daemon is not already running",
				     path, safe_strerror(errno));
			exit(1);
		}

		snprintf(buf, sizeof(buf), "%d\n", (int)pid);
		pidsize = strlen(buf);
		/* 【值得留意的一行】write() 返回 ssize_t，这里与 (int)pidsize 比较。
		 * 在实践中 pid 字符串只有几个字节，(int) 与 ssize_t 数值一致，看不出
		 * 问题；但严格说 ssize_t → int 的隐式/显式截断在 64 位平台上不够严谨，
		 * 也掩盖了“写入不足”时应有的重试逻辑(短写 short write)。此处**只作
		 * 标注、不改代码**，留作读者自行判断。 */
		if ((tmp = write(fd, buf, pidsize)) != (int)pidsize)
			flog_err_sys(
				EC_LIB_SYSTEM_CALL,
				"Could not write pid %d to pid_file %s, rc was %d: %s",
				(int)pid, path, tmp, safe_strerror(errno));
		else if (ftruncate(fd, pidsize) < 0)
			flog_err_sys(
				EC_LIB_SYSTEM_CALL,
				"Could not truncate pid_file %s to %u bytes: %s",
				path, (unsigned int)pidsize,
				safe_strerror(errno));
	}
	return pid;
}
