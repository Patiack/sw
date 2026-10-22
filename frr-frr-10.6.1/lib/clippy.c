// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * clippy (CLI preparator in python) main executable
 * Copyright (C) 2016-2017  David Lamparter for NetDEF, Inc.
 */

#include "config.h"
#include <Python.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "getopt.h"

#include "command_graph.h"
#include "clippy.h"

/*
 * 文件导读：clippy.c —— FRR CLI 帮助文本工具“clippy”的可执行入口
 *
 * 1. 这个文件干什么：
 *    clippy 是一个用 Python 写的代码生成器，读取 FRR 的 CLI 定义（DEFUN /
 *    DEFPY 等），自动推导帮助字符串、命令补全等 C 代码。本文件是它的 C 侧
 *    main()：初始化嵌入式 Python 解释器，把 C 侧函数经扩展模块 "_clippy" 注册
 *    进 Python，然后运行 clippy 的 Python 主脚本。
 *
 * 2. 关键内容：
 *    - main()（分两套写法）：Python 3.8+ 用新的 PyConfig / PyPreConfig API；
 *      Python 3.8 以下用旧的 Py_SetProgramName / PySys_SetArgv API。
 *    - command_py_init：来自 clippy.h，负责注册 _clippy 扩展模块。
 *    - 文件末尾提供精简版日志函数（vzlogx / memory_oom），避免链接 libzebra。
 *
 * 3. 与其他模块的关系：
 *    依赖嵌入式 Python、command_graph.h、clippy.h、log.h；它是构建期工具，
 *    不参与运行时的路由进程。
 *
 * 4. 阅读顺序：
 *    先看 main() 的 Python 3.8+ 分支，再看 < 3.8 的兼容分支，最后看日志桩函数。
 *
 * 5. 易踩的坑：
 *    - 两套 Python C-API 分支随版本变化大，改一处要对照另一处。
 *    - 末尾的日志函数是“桩”，只往 stderr 打印或直接 abort，不链接完整 libzebra。
 *    - 无参数启动时进入交互式 shell，并绑定 readline 的 Tab 补全。
 */
#if PY_VERSION_HEX >= 0x03080000
/*
 * main -- clippy 可执行程序入口（Python 3.8 及以上的新初始化 API 路径）。
 * 流程：先用 PyPreConfig 设置语言环境 / UTF-8 模式，再 Py_PreInitializeFromBytesArgs；
 *       然后用 PyConfig 设置 argv、program_name、run_filename（第 1 个参数作为脚本名）；
 *       通过 PyImport_AppendInittab 注册 _clippy 扩展模块，最后 Py_InitializeFromConfig
 *       完成初始化并 Py_RunMain() 运行 Python 脚本。
 * 参数：argc / argv 为标准命令行参数；argv[1] 若存在则当作待执行的 Python 文件。
 * 返回：Python 解释器退出码；初始化失败时由 Py_ExitStatusException 直接退出。
 */
/* new python init/config API added in Python 3.8 */
int main(int argc, char **argv)
{
	PyStatus status;
	PyPreConfig preconfig[1];
	PyConfig config[1];

	PyPreConfig_InitPythonConfig(preconfig);
	preconfig->configure_locale = 0;
	preconfig->coerce_c_locale = 1;
	preconfig->coerce_c_locale_warn = 0;
	preconfig->isolated = 0;
	preconfig->utf8_mode = 1;
	preconfig->parse_argv = 0;

	status = Py_PreInitializeFromBytesArgs(preconfig, argc, argv);
	if (PyStatus_Exception(status))
		Py_ExitStatusException(status);

	PyConfig_InitPythonConfig(config);
#if PY_VERSION_HEX >= 0x030b0000	/* 3.11 */
	config->safe_path = 0;
#endif

	status = PyConfig_SetBytesArgv(config, argc, argv);
	if (PyStatus_Exception(status))
		Py_ExitStatusException(status);

	PyConfig_SetBytesString(config, &config->program_name,
				argc > 0 ? argv[0] : "clippy");
	if (argc > 1)
		PyConfig_SetBytesString(config, &config->run_filename, argv[1]);

	PyImport_AppendInittab("_clippy", command_py_init);

	status = Py_InitializeFromConfig(config);
	if (PyStatus_Exception(status))
		Py_ExitStatusException(status);

	PyConfig_Clear(config);

	return Py_RunMain();
}

#else /* Python < 3.8 */
/* old python init/config API, deprecated in Python 3.11 */
/*
 * pychar -- Python 3 下 argv 使用宽字符（wchar_t），Python 2 下使用普通 char；
 * wconv 在 Py3 做转换、Py2 直接原样返回，以此屏蔽两代 Python API 的差异。
 */
#if PY_MAJOR_VERSION >= 3
#define pychar wchar_t
/*
 * wconv -- 把多字节窄字符串转成宽字符串（仅 Python 3 路径需要）。
 * 参数 s：源窄字符串，允许为 NULL。返回新分配的宽字符串，调用者负责 free。
 * 坑：先用 mbstowcs(NULL, ...) 探测长度再分配；outlen 为 0 时返回空串。
 */
static wchar_t *wconv(const char *s)
{
	size_t outlen = s ? mbstowcs(NULL, s, 0) : 0;
	wchar_t *out = malloc((outlen + 1) * sizeof(wchar_t));

	if (outlen > 0)
		mbstowcs(out, s, outlen);
	out[outlen] = 0;
	return out;
}
#else
#define pychar char
#define wconv(x) x
#endif

/*
 * main -- clippy 入口的旧版实现（Python 3.8 以下；此 API 自 3.11 起被弃用）。
 * 流程：用 wconv 把窄字符串转成宽字符串，Py_SetProgramName 设置程序名并注册
 *       _clippy 模块；Py_Initialize 后通过 PySys_SetArgv 把参数传给 Python。
 *       若给了脚本文件则打开执行，否则进入交互式 shell（读 stdin 并绑定 readline 补全）。
 * 返回：0 成功；1 失败（文件打不开或脚本抛异常）。
 * 坑：宽字符串内存需逐个 free；注意 Py3 与 Py2 的 pychar / wconv 差异。
 */
int main(int argc, char **argv)
{
	pychar **wargv;

#if PY_VERSION_HEX >= 0x03040000 /* 3.4 */
	Py_SetStandardStreamEncoding("UTF-8", NULL);
#endif
	wchar_t *name = wconv(argv[0]);
	Py_SetProgramName(name);
	PyImport_AppendInittab("_clippy", command_py_init);

	Py_Initialize();

	wargv = malloc(argc * sizeof(pychar *));
	for (int i = 1; i < argc; i++)
		wargv[i - 1] = wconv(argv[i]);
	PySys_SetArgv(argc - 1, wargv);

	const char *pyfile = argc > 1 ? argv[1] : NULL;
	FILE *fp;
	if (pyfile) {
		fp = fopen(pyfile, "r");
		if (!fp) {
			fprintf(stderr, "%s: %s\n", pyfile, strerror(errno));

			free(name);
			return 1;
		}
	} else {
		fp = stdin;
		char *ver = strdup(Py_GetVersion());
		char *cr = strchr(ver, '\n');
		if (cr)
			*cr = ' ';
		fprintf(stderr, "clippy interactive shell\n(Python %s)\n", ver);
		free(ver);
		PyRun_SimpleString(
			"import rlcompleter, readline\n"
			"readline.parse_and_bind('tab: complete')");
	}

	if (PyRun_AnyFile(fp, pyfile)) {
		if (PyErr_Occurred())
			PyErr_Print();

		free(name);
		return 1;
	}
	Py_Finalize();

#if PY_MAJOR_VERSION >= 3
	for (int i = 1; i < argc; i++)
		free(wargv[i - 1]);
#endif
	free(name);
	free(wargv);
	return 0;
}
#endif /* Python < 3.8 */

/*
 * 以下为日志 / 内存“桩函数”区：clippy 作为构建期工具不应链接 libzebra
 * （否则会造成循环构建依赖），故在此提供简化的 vzlogx / memory_oom 实现。
 */
/* and now for the ugly part... provide simplified logging functions so we
 * don't need to link libzebra (which would be a circular build dep) */

#include "log.h"

PRINTFRR(3, 0)
/*
 * vzlogx -- 精简版日志实现桩：把格式化结果直接写到 stderr 并换行。
 * 存在原因：clippy 是构建期工具，链接完整 libzebra 会造成循环依赖，
 *           因此仅提供 lib 代码（如 command_graph）所需的最小符号。
 * 参数：xref 关联信息（此处忽略）、prio 级别（忽略）、format + args 为输出内容。
 */
void vzlogx(const struct xref_logmsg *xref, int prio,
	    const char *format, va_list args)
{
	vfprintf(stderr, format, args);
	fputs("\n", stderr);
}

/*
 * memory_oom -- 内存分配失败的桩实现，直接 abort()。
 * 同样是避免链接完整 libzebra 的最小替身；一旦触发即崩溃退出。
 * 参数：size 请求大小、name 分配名（此处均未使用）。
 */
void memory_oom(size_t size, const char *name)
{
	abort();
}
