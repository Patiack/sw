/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Command format string lexer for CLI backend.
 *
 * --
 * Copyright (C) 2015 Cumulus Networks, Inc.
 */

/*
 * 【文件导读】command_lex.l —— CLI 命令“定义串”的词法分析器(flex 源文件)
 * =====================================================================
 * 【产物】flex 由本文件生成 lib/command_lex.c 与 lib/command_lex.h。它把一条命令
 *   定义字符串切成 token,交给 command_parse.y 生成的语法分析器。
 *
 * 【与语法分析器的配合】采用 reentrant(可重入)+ bison-bridge + bison-locations:
 *   - 可重入:扫描器状态放在 yyscan_t 中,而非全局变量;
 *   - bison-bridge:yylex 以便参数 yylval / yylloc 与 yacc 交换语义值 / 位置;
 *   - locations:每 token 记录列号(由 YY_USER_ACTION 递增 last_column)。
 *
 * 【识别的东西】
 *   - 空白:忽略(仅推进列号)
 *   - 命名的模式(见“定义”段):ASNUM / IPV4(_PREFIX) / IPV6(_PREFIX) /
 *     MAC(_PREFIX) / VARIABLE / WORD / RANGE,分别返回对应 token,并把文本用
 *     XSTRDUP 存入 yylval.string(由语法侧负责释放)
 *   - "![" :返回 EXCL_BRACKET(用于 ![ ] 否定式可选)
 *   - 其它任意单字符:原样返回该字符(供 yacc 用 'x' 终结符匹配,如 '<' '>' '|')
 *
 * 【易踩的坑】命名模式里的 A B C D X M 等是字面字母,不是通配;因此该词法器只认
 *   命令定义串里“占位符”的标准写法(如 A.B.C.D),并不解析真实地址。
 * =====================================================================
 */
/* 【top 代码】%top 块被放到生成文件的最顶部(在 include 之前)——这里放入
 * config.h,保证宏在一切之前生效。 */
%top{
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
}
/* 【C 声明】%{ %} 的内容原样插入生成的 .c。
 *   - 用 __clang_analyzer__ 宏把整份文件对静态分析器屏蔽(生成代码噪声大);
 *   - pragma 关掉 flex 旧版本生成代码里的若干无害告警;
 *   - 引入 command_parse.h(语义值 / 类型定义);
 *   - YY_USER_ACTION:每匹配一个 token 后把 last_column 前移 yyleng;
 *   - LOC_STEP:推进位置区间(first_* 取 last_*),用于 yylloc。 */
%{
/* ignore flex generated code in static analyzer */
#ifndef __clang_analyzer__

/* ignore harmless bugs in old versions of flex */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

#include "lib/command_parse.h"

#define YY_USER_ACTION yylloc->last_column += yyleng;
#define LOC_STEP do { if (yylloc) { \
        yylloc->first_column = yylloc->last_column; \
        yylloc->first_line = yylloc->last_line; \
	} } while(0)
%}

/* 【定义:命名模式】以下为可在规则里用 {NAME} 引用的正则片段。
 *   IPV4 / IPV4_PREFIX : A.B.C.D 与 A.B.C.D/M(字面,非真实地址)
 *   IPV6 / IPV6_PREFIX : X:X::X:X 与 X:X::X:X/M
 *   MAC / MAC_PREFIX   : X:X:X:X:X:X 与带 /M 的 MAC
 *   VARIABLE           : 大写字母开头的变量名(可含 - _ 与数字)
 *   WORD               : 普通词(可选 +/- 前缀)
 *   NUMBER             : 十进制数(可选 +/- 前缀)
 *   RANGE              : (NUMBER-NUMBER) 形式,供 (0-65535) 之类使用
 *   ASNUM              : 字面 "ASNUM"(AS 号占位) */
IPV4            A\.B\.C\.D
IPV4_PREFIX     A\.B\.C\.D\/M
IPV6            X:X::X:X
IPV6_PREFIX     X:X::X:X\/M
MAC             X:X:X:X:X:X
MAC_PREFIX      X:X:X:X:X:X\/M
VARIABLE        [A-Z][-_A-Z:0-9]+
WORD            (\-|\+)?[a-zA-Z0-9\*][-+_a-zA-Z0-9\*]*
NUMBER          (\-|\+)?[0-9]{1,20}
RANGE           \({NUMBER}[ ]?\-[ ]?{NUMBER}\)
ASNUM           ASNUM

/* yytext shall be a pointer */
/* 【flex 选项】
 *   %pointer        : yytext 用指针(而非数组)实现;
 *   noyywrap        : 单输入源,不需要 yywrap;
 *   nounput / noinput : 不使用 unput / input,缩小生成代码;
 *   outfile / header-file / prefix : 生成路径与符号前缀 cmd_yy;
 *   reentrant + bison-bridge + bison-locations : 见文件导读。 */
%pointer
%option noyywrap
%option nounput
%option noinput
%option outfile="lib/command_lex.c"
%option header-file="lib/command_lex.h"
%option prefix="cmd_yy"
%option reentrant
%option bison-bridge
%option bison-locations

%%
/* 【规则段】%% 之后为词法规则。先执行一个动作块:每次进入扫描循环时分两步推进
 * 位置(LOC_STEP),使 yylloc 的区间正确。 */
%{
                LOC_STEP;
%}


/* 【规则】空白(空格 / Tab)忽略,仅 LOC_STEP 推进列号。 */
[ \t]+          LOC_STEP /* ignore whitespace */;
/* 【规则】AS 号占位 -> 返回 ASNUM;文本 XSTRDUP 进 yylval.string。 */
{ASNUM}         {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return ASNUM;}
/* 【规则】IPv4 地址占位 A.B.C.D -> 返回 IPV4。 */
{IPV4}          {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return IPV4;}
/* 【规则】IPv4 前缀占位 A.B.C.D/M -> 返回 IPV4_PREFIX。 */
{IPV4_PREFIX}   {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return IPV4_PREFIX;}
/* 【规则】IPv6 地址占位 X:X::X:X -> 返回 IPV6。 */
{IPV6}          {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return IPV6;}
/* 【规则】IPv6 前缀占位 X:X::X:X/M -> 返回 IPV6_PREFIX。 */
{IPV6_PREFIX}   {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return IPV6_PREFIX;}
/* 【规则】MAC 地址占位 X:X:X:X:X:X -> 返回 MAC。 */
{MAC}           {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return MAC;}
/* 【规则】带掩码 MAC 占位 X:X:X:X:X:X/M -> 返回 MAC_PREFIX。 */
{MAC_PREFIX}    {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return MAC_PREFIX;}
/* 【规则】变量占位(大写名)-> 返回 VARIABLE。 */
{VARIABLE}      {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return VARIABLE;}
/* 【规则】普通词(命令关键字)-> 返回 WORD。 */
{WORD}          {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return WORD;}
/* 【规则】数值区间 (m-n) -> 返回 RANGE(区间解析在语法侧完成)。 */
{RANGE}         {yylval->string = XSTRDUP(MTYPE_LEX, yytext); return RANGE;}
/* 【规则】匹配 "![" 两字符 -> 返回 EXCL_BRACKET(供 ![ ] 否定式可选;值置 NULL)。 */
!\[             {yylval->string = NULL; return EXCL_BRACKET;}
/* 【规则】兜底:其它任意单字符原样返回其本身(如 '<' '>' '|' '$' '.' 等,
 * 供 yacc 里以字符终结符匹配)。 */
.               {return yytext[0];}
%%

/* 【用户子例程段(%% 之后)】以下函数被 command_parse.y 的 cmd_graph_parse 调用,
 * 用于把待解析字符串喂给扫描器并释放。buffer 保存当前扫描缓冲。 */
static YY_BUFFER_STATE buffer;

/* 【接口】初始化扫描器并让它扫描给定字符串。
 * 参数 scn    : 输出——新的 yyscan_t 句柄。
 * 参数 string : 待扫描的定义串。
 * 副作用：yylex_init 建扫描器;yy_scan_string 建缓冲并存入文件级 buffer。
 * 【易踩的坑】buffer 是文件级单例,同一时刻只能有一个活跃扫描串。 */
void set_lexer_string (yyscan_t *scn, const char *string)
{
  *scn = NULL;
  yylex_init(scn);
  buffer = yy_scan_string (string, *scn);
}

/* 【接口】销毁扫描器,释放其资源。
 * 参数 scn : 待销毁的 yyscan_t 句柄。
 * 【易踩的坑】本函数只 yylex_destroy,不显式删除 buffer(删除 buffer 的那行被注释
 *   掉了),因为 destroy 会连带清理。 */
void cleanup_lexer (yyscan_t *scn)
{
  // yy_delete_buffer (buffer, *scn);
  yylex_destroy(*scn);
}

#endif /* __clang_analyzer__ */
