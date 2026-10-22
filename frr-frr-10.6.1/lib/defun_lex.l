/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * clippy (CLI preparator in python) C pseudo-lexer
 * Copyright (C) 2016-2017  David Lamparter for NetDEF, Inc.
 */

/* This is just enough of a lexer to make rough sense of a C source file.
 * It handles C preprocessor directives, strings, and looks for FRR-specific
 * idioms (aka DEFUN).
 *
 * There is some preliminary support for documentation comments for DEFUNs.
 * They would look like this (note the ~):  (replace \ by /)
 *
 * \*~  documentation for foobar_cmd
 *  *   parameter does xyz
 *  *\
 * DEFUN(foobar_cmd, ...)
 *
 * This is intended for user documentation / command reference.  Don't put
 * code documentation in it.
 */

/*
 * 【文件导读】defun_lex.l —— clippy(CLI 预处理器)用的“C 伪词法器”(flex 源文件)
 * =====================================================================
 * 【产物】flex 由本文件生成 lib/defun_lex.c;它与 command_py.c(模块 _clippy)配套,
 *   由 Python 侧 clippy 调用。本词法器的任务不是真正编译 C,而是从 **C 源码**里
 *   挑出与命令定义相关的片段:DEFUN / DEFPY / ALIAS 等宏、install_element 调用,
 *   以及注释 / 预处理指令,交给 Python 生成命令表。
 * 【注意】这与 command_lex.l 不同:那个解析的是“命令定义串”,本文件解析的是
 *   “C 源代码文件”。它只做到“够用的粗略理解”,不构造完整 C 语法树。
 *
 * 【识别的东西】
 *   - 预处理器指令(# 到行尾,支持反斜杠续行)-> PREPROC
 *   - 字符串与字符字面量(含转义、跨行报警)-> STRING
 *   - 块注释与行注释(含 clippy 文档注释形式)-> COMMENT
 *   - 命令定义宏名(DEFUN / DEFPY / DEFPY_YANG / ALIAS 等)-> DEFUNNY
 *   - install_element() -> INSTALL;VTYSH_TARGETS / VTYSH_NODESWITCH -> AUXILIARY
 *   - 普通标识符 -> ID;运算符 -> OPERATOR;括号 / 逗号 -> SPECIAL
 *
 * 【起始条件(start conditions)】
 *   %s linestart      : 包容式的“行首”状态(用于识别行首的预处理指令)
 *   %x comment / linecomment / preproc / rstring : 独占式子状态,
 *     分别处理块注释、行注释、预处理指令、字符串;用 BEGIN(...) 切换,用
 *     YY_START / comment_link 记住并恢复。
 *
 * 【阅读顺序】头部声明与辅助 -> 命名模式与 %option -> 起始条件声明
 *   -> 规则(注释 / 预处理 / 字符串 / 宏名 / 通用 token)-> %% 后的 C 代码
 *   (yylex_clr / get_args / clippy_parse)。
 * =====================================================================
 */
/* 【top 代码】%top 块置于生成文件最顶部:包含 config.h。 */
%top{
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
}
/* 【C 声明】%{ %} 内容原样插入生成的 .c:
 *   - 关掉 flex 旧版本的若干无害告警;
 *   - 引入 config.h / Python.h / command_graph.h / clippy.h;
 *   - 定义各 token 编号(ID/PREPROC/OPERATOR/STRING/COMMENT/SPECIAL 及
 *     DEFUNNY/INSTALL/AUXILIARY);
 *   - 全局:comment_link(记录进入注释前的状态,供恢复)、string_end(字符串的
 *     结束引号)、value(当前累积的文本)、yyfilename(当前文件名);
 *   - 辅助:extendbuf / extend 追加字符串缓冲区。
 *   - 用 __clang_analyzer__ 把实用代码与生成代码一分为二(见文件末的 #else)。 */
%{
/* ignore harmless bugs in old versions of flex */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

#include "config.h"
#include <Python.h>
#include <string.h>
#include <stdlib.h>

#include "command_graph.h"
#include "clippy.h"

/* 【token 编号】本词法器手工分配两类 token 号:
 *   词法类:ID / PREPROC / OPERATOR / STRING / COMMENT / SPECIAL
 *   DEFUN 类:DEFUNNY / INSTALL / AUXILIARY
 * 这些编号与 command_py.c(clippy_parse)里的 switch 分支对应。 */
#define ID		258
#define PREPROC		259
#define OPERATOR	260
#define STRING		261
#define COMMENT		262
#define SPECIAL		263

#define DEFUNNY		270
#define INSTALL		271
#define AUXILIARY	272

/* 【全局状态】
 *   comment_link : 进入注释前所处的起始条件(YY_START),用于注释结束后恢复;
 *   string_end   : 当前字符串 / 字符字面量使用的结束引号(双引号或单引号);
 *   value        : 累积当前 token 文本的缓冲(与 extend 宏配合);
 *   yyfilename   : 当前被扫描的文件名(报错定位用)。 */
int comment_link;
char string_end;

char *value;
static const char *yyfilename;

/* 【辅助】把 arg 追加到 *what 缓冲区(首次 strdup,之后 realloc 扩展)。
 * 参数 what : 目标缓冲指针的地址;arg : 追加内容。
 * 副作用：可能重新分配 *what。 */
static void extendbuf(char **what, const char *arg)
{
	if (!*what)
		*what = strdup(arg);
	else {
		size_t vall = strlen(*what), argl = strlen(arg);
		*what = realloc(*what, vall + argl + 1);
		memcpy(*what + vall, arg, argl);
		(*what)[vall + argl] = '\0';
	}
}
/* 【宏】extend(x):把 x 追加到全局 value 缓冲(见 extendbuf)。 */
#define extend(x) extendbuf(&value, x)

#ifndef __clang_analyzer__

%}

/* 【定义:命名模式】
 *   ID       : C 标识符(字母数字下划线)
 *   OPERATOR : 各类运算符 / 标点(仅用于切分,不解析语义)
 *   SPECIAL  : 仅 ( ) , —— 参数列表分隔,单独分类便于 get_args 处理 */
ID		[A-Za-z0-9_]+
OPERATOR	[!%&/\[\]{}=?:^|\*.;><~'\\+-]
SPECIAL		[(),]

/* 【flex 选项】
 *   %pointer        : yytext 用指针实现;
 *   yylineno        : 维护行号(报错用 yylineno);
 *   noyywrap / noinput / nounput : 精简生成代码;
 *   outfile / prefix : 生成路径与符号前缀 def_yy;
 *   8bit            : 使用 8 位字符表。 */
%pointer
%option yylineno
%option noyywrap
%option noinput
%option nounput
%option outfile="lib/defun_lex.c"
%option prefix="def_yy"
%option 8bit

/* 【起始条件声明】%s 为包容式(可与其它状态共存),%x 为独占式:
 *   linestart   : 行首标记(用于识别行首的 # 预处理指令);
 *   comment     : 块注释内部;
 *   linecomment : 行注释内部;
 *   preproc     : 预处理指令内部;
 *   rstring     : 字符串 / 字符字面量内部。 */
%s linestart
%x comment
%x linecomment
%x preproc
%x rstring
/* 【规则段入口】%% 之下的第一段动作在扫描开始前执行一次:进入 linestart 状态,
 * 使文件开头(可能紧跟预处理指令)能被正确识别。 */
%%
				BEGIN(linestart);

/* 【规则】遇到换行符:进入 linestart,以便识别下一行行首的预处理指令。 */
\n				BEGIN(linestart);

/* 【规则】块注释起始(两个字符):在 INITIAL / linestart / preproc 状态下识别。
 * 记住当前状态到 comment_link,把起始符并入缓冲,切到 comment 状态。 */
<INITIAL,linestart,preproc>"/*"	comment_link = YY_START; extend(yytext); BEGIN(comment);
/* 【规则】块注释体:一段既非星号也非换行的字符,累积到缓冲。 */
<comment>[^*\n]*		extend(yytext);
/* 【规则】块注释体:星号(可多个)后接非星非斜杠字符,累积到缓冲。 */
<comment>"*"+[^*/\n]*		extend(yytext);
/* 【规则】块注释内的换行:累积到缓冲(注释可跨行)。 */
<comment>\n			extend(yytext);
/* 【规则】块注释结束符(星号后接斜杠):累积后恢复到进入注释前的状态,
 * 返回 COMMENT(整段注释文本在 value 中)。 */
<comment>"*"+"/"		extend(yytext); BEGIN(comment_link); return COMMENT;

/* 【规则】行注释起始(两个斜杠):同样记状态并切到 linecomment。 */
<INITIAL,linestart,preproc>"//"	comment_link = YY_START; extend(yytext); BEGIN(linecomment);
/* 【规则】行注释体:到行尾之前的全部字符,累积到缓冲。 */
<linecomment>[^\n]*		extend(yytext);
/* 【规则】行注释遇换行即结束:若原来在 INITIAL 则回到 linestart(便于识别
 * 下一行行首的预处理指令),否则回到原状态;返回 COMMENT。 */
<linecomment>\n			BEGIN((comment_link == INITIAL) ? linestart : comment_link); return COMMENT;

/* 【规则】行首的 # 号:进入预处理指令状态。 */
<linestart>#			BEGIN(preproc);
/* 【规则】预处理指令遇换行结束:回到 INITIAL,返回 PREPROC。 */
<preproc>\n			BEGIN(INITIAL); return PREPROC;
/* 【规则】预处理指令体:不含换行与反斜杠的一段字符,累积到缓冲。 */
<preproc>[^\n\\]+		extend(yytext);
/* 【规则】预处理指令的续行(反斜杠接换行):累积到缓冲,指令继续。 */
<preproc>\\\n			extend(yytext);
/* 【规则】预处理指令里的反斜杠序列(一个或多个反斜杠后接非换行字符):
 * 累积到缓冲,配合上一条处理各类续行 / 转义写法。 */
<preproc>\\+[^\n]		extend(yytext);

/* 【规则】字符串 / 字符字面量起始:记下结束引号(单引号或双引号),累积该引号并切到 rstring 状态。 */
[\"\']				string_end = yytext[0]; extend(yytext); BEGIN(rstring);
/* 【规则】rstring 内再次遇到引号:若与起始引号相同则结束字符串(回到 INITIAL 并返回 STRING),否则当作普通内容继续累积。 */
<rstring>[\"\']			{
					extend(yytext);
					if (yytext[0] == string_end) {
						BEGIN(INITIAL);
						return STRING;
					}
				}
/* 【规则】rstring 内的反斜杠续行:忽略(不并入缓冲)。 */
<rstring>\\\n			/* ignore */
/* 【规则】rstring 内遇到裸换行(字符串跨行):报错到 stderr,释放缓冲,
 * 回到 INITIAL 并返回 STRING(clippy 据此提示字符串未闭合)。 */
<rstring>\n			{
					fprintf(stderr,
						"%s:%d: string continues past the end of the line\n",
						yyfilename, yylineno);
					free(value);
					value = NULL;
					BEGIN(INITIAL);
					return STRING;
				}
/* 【规则】rstring 内的转义序列(反斜杠接任意字符):累积到缓冲。 */
<rstring>\\.			extend(yytext);
/* 【规则】rstring 体:非反斜杠、非引号、非换行的字符,累积到缓冲。 */
<rstring>[^\\\"\'\n]+		extend(yytext);

/* 【规则】命令定义宏名(clippy 关注的 FRR 专用写法):命中即返回 DEFUNNY,
 * 并把宏名存入 value。覆盖 DEFUN / DEFUN_NOSH / DEFUN_HIDDEN、DEFPY 各变体
 * (_NOSH / _ATTR / _HIDDEN / _YANG / _YANG_HIDDEN / _YANG_NOSH)
 * 以及 ALIAS / ALIAS_HIDDEN。Python 侧据此识别命令定义。 */
"DEFUN"				value = strdup(yytext); return DEFUNNY;
"DEFUN_NOSH"			value = strdup(yytext); return DEFUNNY;
"DEFUN_HIDDEN"			value = strdup(yytext); return DEFUNNY;
"DEFPY"				value = strdup(yytext); return DEFUNNY;
"DEFPY_NOSH"			value = strdup(yytext); return DEFUNNY;
"DEFPY_ATTR"			value = strdup(yytext); return DEFUNNY;
"DEFPY_HIDDEN"			value = strdup(yytext); return DEFUNNY;
"DEFPY_YANG"			value = strdup(yytext); return DEFUNNY;
"DEFPY_YANG_HIDDEN"		value = strdup(yytext); return DEFUNNY;
"DEFPY_YANG_NOSH"		value = strdup(yytext); return DEFUNNY;
"ALIAS"				value = strdup(yytext); return DEFUNNY;
"ALIAS_HIDDEN"			value = strdup(yytext); return DEFUNNY;
/* 【规则】install_element 返回 INSTALL;VTYSH_TARGETS / VTYSH_NODESWITCH
 * 返回 AUXILIARY(命令安装与目标 vtysh 的辅助标记)。 */
"install_element"		value = strdup(yytext); return INSTALL;
"VTYSH_TARGETS"			value = strdup(yytext); return AUXILIARY;
"VTYSH_NODESWITCH"		value = strdup(yytext); return AUXILIARY;

/* 【规则】空白(空格 / Tab / 换行)忽略。 */
[ \t\n]+			/* ignore */
/* 【规则】孤立的反斜杠(不在字符串 / 宏上下文中)忽略。 */
\\				/* ignore */
/* 【规则】通用 token:空白(空格 / Tab / 换行)与孤立反斜杠忽略;
 * 标识符 -> ID;运算符 -> OPERATOR;括号 / 逗号 -> SPECIAL;
 * 其它任意单字符原样返回其本身(供 Python 侧按字符判断)。
 * 每条命中后均回到 INITIAL 并把文本存入 value。 */
{ID}				BEGIN(INITIAL); value = strdup(yytext); return ID;
/* 【规则】运算符 -> OPERATOR。 */
{OPERATOR}			BEGIN(INITIAL); value = strdup(yytext); return OPERATOR;
/* 【规则】括号 / 逗号 -> SPECIAL(get_args 依赖它切分实参)。 */
{SPECIAL}			BEGIN(INITIAL); value = strdup(yytext); return SPECIAL;
/* 【规则】兜底:其它任意单字符原样返回其本身(供 Python 侧按字符判断)。 */
.				/* printf("-- '%s' in init\n", yytext); */ BEGIN(INITIAL); return yytext[0];

%%

/* 【可选分支】当定义了 __clang_analyzer__ 时(静态分析器),不生成真正的扫描器,
 * 只声明 def_yylex / def_yylex_destroy 原型,避免分析器陷入 flex 生成码。 */
#else
extern int def_yylex(void);
extern int def_yylex_destroy(void);
#endif /* __clang_analyzer__ */

/* 【辅助】取下一个 token 并交出文本。
 * 参数 retbuf : 输出——本次 token 的文本(取自全局 value),随后 value 置空。
 * 返回 : def_yylex() 的 token 号(YY_NULL 表示结束)。
 * 【易踩的坑】调用者负责 free(*retbuf)。 */
static int yylex_clr(char **retbuf)
{
	int rv = def_yylex();
	*retbuf = value;
	value = NULL;
	return rv;
}

/* 【辅助】解析一条命令定义宏 / install_element 的实参列表。
 * 参数 filename : 文件名(仅用于报错);
 * 参数 lineno   : 行号(仅用于报错)。
 * 返回 : 新的 Python 列表,每项对应一个实参(该实参又由若干 token 组成的列表
 *   表示);出错返回 NULL 并设置 Python 异常。
 * 处理:先推进到左括号(在此之前若出现非注释 token 视为非法输入并退出);
 *   随后按括号深度收集 token,在顶层逗号 / 右括号处切分一个实参;
 *   实参中出现预处理指令则报 ValueError。
 * 【易踩的坑】返回值与各实参列表的引用计数由 Python 侧管理。 */
static PyObject *get_args(const char *filename, int lineno)
{
	PyObject *pyObj = PyList_New(0);
	PyObject *pyArg = NULL;

	char *tval;
	int depth = 1;
	int token;

	while ((token = yylex_clr(&tval)) != YY_NULL) {
		if (token == SPECIAL && tval[0] == '(') {
			free(tval);
			break;
		}
		if (token == COMMENT) {
			free(tval);
			continue;
		}
		fprintf(stderr, "invalid input!\n");
		exit(1);
	}

	while ((token = yylex_clr(&tval)) != YY_NULL) {
		if (token == COMMENT) {
			free(tval);
			continue;
		}
		if (token == PREPROC) {
			free(tval);
			Py_DECREF(pyObj);
			return PyErr_Format(PyExc_ValueError,
					"%s:%d: cannot process CPP directive within argument list",
					filename, lineno);
		}
		if (token == SPECIAL) {
			if (depth == 1 && (tval[0] == ',' || tval[0] == ')')) {
				if (pyArg)
					PyList_Append(pyObj, pyArg);
				pyArg = NULL;
				if (tval[0] == ')') {
					free(tval);
					break;
				}
				free(tval);
				continue;
			}
			if (tval[0] == '(')
				depth++;
			if (tval[0] == ')')
				depth--;
		}
		if (!tval)
			return PyErr_Format(PyExc_ValueError,
					"%s:%d: invalid token in DEFPY parameters",
					filename, lineno);
		if (!pyArg)
			pyArg = PyList_New(0);
		PyList_Append(pyArg, PyUnicode_FromString(tval));
		free(tval);
	}
	return pyObj;
}

/* 【门面】clippy_parse:_clippy.parse() 的 C 实现——读一个 C 源文件并返回
 * “感兴趣片段”的字典。
 * 参数 filename : 待解析的 C 文件名。
 * 返回 : dict {filename, data:[{type, args|line, lineno}, ...]}:其中
 *   DEFUNNY / INSTALL / AUXILIARY 项带 args(实参列表,由 get_args 解析),
 *   注释 / 预处理项带 line(原文)。
 * 副作用:打开并读取文件;仅对文档注释(以 //~ 或文档注释专用起始标记开头者)
 *   建立条目,普通注释被丢弃;结束时销毁扫描器、关闭文件。
 * 【易踩的坑】PREPROC 条目的 lineno 会做一次减一修正。 */
/* _clippy.parse() -- read a C file, returning a list of interesting bits.
 * note this ditches most of the actual C code. */
PyObject *clippy_parse(PyObject *self, PyObject *args)
{
	const char *filename;
	if (!PyArg_ParseTuple(args, "s", &filename))
		return NULL;
	
	FILE *fd = fopen(filename, "r");
	if (!fd)
		return PyErr_SetFromErrnoWithFilename(PyExc_IOError, filename);

	char *tval;
	int token;
	yyin = fd;
	value = NULL;
	yyfilename = filename;

	PyObject *pyCont = PyDict_New();
	PyObject *pyObj = PyList_New(0);
	PyDict_SetItemString(pyCont, "filename", PyUnicode_FromString(filename));
	PyDict_SetItemString(pyCont, "data", pyObj);

	while ((token = yylex_clr(&tval)) != YY_NULL) {
                int lineno = yylineno;
		PyObject *pyItem = NULL, *pyArgs;
		switch (token) {
		case DEFUNNY:
		case INSTALL:
		case AUXILIARY:
			pyArgs = get_args(filename, lineno);
			if (!pyArgs) {
				free(tval);
				Py_DECREF(pyCont);
				yyfilename = NULL;
				return NULL;
			}
			pyItem = PyDict_New();
			PyDict_SetItemString(pyItem, "type", PyUnicode_FromString(tval));
			PyDict_SetItemString(pyItem, "args", pyArgs);
			break;
		case COMMENT:
                        if (strncmp(tval, "//~", 3) && strncmp(tval, "/*~", 3))
                                break;
			pyItem = PyDict_New();
			PyDict_SetItemString(pyItem, "type", PyUnicode_FromString("COMMENT"));
			PyDict_SetItemString(pyItem, "line", PyUnicode_FromString(tval));
			break;
		case PREPROC:
			pyItem = PyDict_New();
			PyDict_SetItemString(pyItem, "type", PyUnicode_FromString("PREPROC"));
			PyDict_SetItemString(pyItem, "line", PyUnicode_FromString(tval));
			lineno--;
			break;
		}
		if (pyItem) {
			PyDict_SetItemString(pyItem, "lineno", PyLong_FromLong(lineno));
			PyList_Append(pyObj, pyItem);
		}
		free(tval);
	}
	def_yylex_destroy();
	fclose(fd);
	yyfilename = NULL;
	return pyCont;
}
