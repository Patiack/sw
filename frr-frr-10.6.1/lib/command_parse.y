// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Command format string parser for CLI backend.
 *
 * --
 * Copyright (C) 2016 Cumulus Networks, Inc.
 */

/*
 * 【文件导读】command_parse.y —— CLI 命令“定义串”的语法分析器(bison 源文件)
 * =====================================================================
 * 【产物】bison 由本文件生成 lib/command_parse.c 与 lib/command_parse.h;
 *   词法分析器由 command_lex.l 生成。二者把一条命令定义字符串(如
 *   "show ip route A.B.C.D")解析成一张 goto 图(command_graph),图中每个节点是
 *   一个 cmd_token;命令匹配时即在此图上走状态机。
 *
 * 【输入 / 输出】入口是文件末尾的 cmd_graph_parse():接收一个 struct graph 与一条
 *   struct cmd_element,用“空格分隔 + 特殊记号”的语法把定义串编译成图:
 *   - 普通单词           -> WORD_TKN(字面量)
 *   - A.B.C.D / X:X::X:X / VARIABLE / RANGE 等 -> 对应占位符 token
 *   - <a|b> 表示“选择其一”,{a|b} 表示“至少其一”,[a|b] 表示“可选”,
 *     ![a] 表示“否定式可选”(NEG_ONLY)
 *   - '...' 表示前一元为可变参(允许重复)
 * 【为何用图】用 goto 图而非正则 / 列表,能正确处理嵌套的选择 / 可选 / 可变参,
 *   并在匹配时支持最短路径与回退。
 *
 * 【本文件结构】
 *   %{ ... %}        : 顶部 C 声明(打开 YYDEBUG)
 *   %code requires   : 生成的 .h 与 .c 都需要的声明(MTYPE、struct subgraph)
 *   %union / %code provides : 语义值类型与 scanner 结构体 struct parser_ctx
 *   %token / %type   : 终结符(来自词法器)与非终结符的类型标注
 *   %code { ... }    : 静态辅助函数的前置声明
 *   %% 语法规则 %%   : 核心产生式(见下方逐条注释)
 *   %% 之后的 C 代码 : cmd_graph_parse、loopcheck、yyerror、doc_next 等实现
 *
 * 【易踩的坑】命令行里 token 之间以空格分隔;定义串里若含引号包围的“整段字面量”,
 *   由 command_lex.l 处理。对可变参 '...' 的建模是在节点上自环(见 start 规则)。
 * =====================================================================
 */
/* 【顶部 C 声明】打开 bison 调试设施 YYDEBUG;此代码块被原样插入生成解析器顶部。 */
%{
// compile with debugging facilities
#define YYDEBUG 1
%}

/* 【bison 指令】%locations:启用位置跟踪(每 token 记录行列),供 yyerror 定位。 */
%locations
/* define parse.error verbose */
/* 【bison 指令】%define api.pure full:生成可重入(纯)解析器,
 * 状态经参数传入而非全局,便于多次 / 可重入解析。 */
%define api.pure full
/* define api.prefix {cmd_yy} */

/* names for generated header and parser files */
/* 【bison 指令】指定生成的头文件与源文件路径。 */
%defines "lib/command_parse.h"
%output  "lib/command_parse.c"

/* note: code blocks are output in order, to both .c and .h:
 *  1. %code requires
 *  2. %union + bison forward decls
 *  3. %code provides
 * command_lex.h needs to be included at 3.; it needs the union and YYSTYPE.
 * struct parser_ctx is needed for the bison forward decls.
 */
/* 【代码块 requires】同时插入生成的 .h 与 .c,且先于 %union:因此这里只能放不依赖
 * 语义值类型的声明。含标准头、command_graph.h、log.h;声明 MTYPE LEX;
 * 定义 struct subgraph(子图 = 起止节点对);并前置声明 struct parser_ctx。 */
%code requires {
  #include "config.h"

  #include <stdbool.h>
  #include <stdlib.h>
  #include <string.h>
  #include <ctype.h>

  #include "command_graph.h"
  #include "log.h"

  DECLARE_MTYPE(LEX);

  #define YYSTYPE CMD_YYSTYPE
  #define YYLTYPE CMD_YYLTYPE
  struct parser_ctx;

  /* subgraph semantic value */
  struct subgraph {
    struct graph_node *start, *end;
  };
}

/* 【语义值联合】%union:所有终结符 / 非终结符共用的语义值类型。
 *   number / string / node / subgraph 分别对应数值、词法串、单个节点、子图。 */
%union {
  long long number;
  char *string;
  struct graph_node *node;
  struct subgraph subgraph;
}

/* 【代码块 provides】插入生成的 .h 与 .c,且在 %union 之后(故可依赖 YYSTYPE)。
 * 含 command_lex.h(它需要 union 与 YYSTYPE);声明 set_lexer_string / cleanup_lexer;
 * 给出 struct parser_ctx 完整定义(scanner、当前元素 el、图 graph、当前节点 currnode、
 * docstr 指针、retval)。 */
%code provides {
  #ifndef FLEX_SCANNER
  #include "lib/command_lex.h"
  #endif

  extern void set_lexer_string (yyscan_t *scn, const char *string);
  extern void cleanup_lexer (yyscan_t *scn);

  struct parser_ctx {
    yyscan_t scanner;

    const struct cmd_element *el;

    struct graph *graph;
    struct graph_node *currnode;

    /* pointers to copy of command docstring */
    char *docstr_start, *docstr;

    uint32_t retval;
  };
}

/* union types for lexed tokens */
/* 【终结符】由 command_lex.l 词法器返回的 token,均携带字符串语义值(string)。
 *   WORD                        : 普通词(可含 + - _ 等),命令关键字字面量
 *   IPV4 / IPV4_PREFIX          : IPv4 地址 / 前缀占位
 *   IPV6 / IPV6_PREFIX          : IPv6 地址 / 前缀占位
 *   VARIABLE                    : 变量占位(大写名形式)
 *   RANGE                       : 数值区间形式 (m-n)
 *   MAC / MAC_PREFIX            : MAC 地址 / 带掩码 MAC 占位
 *   ASNUM                       : AS 号占位
 * 说明:文法里它们多数会被转成对应的 *_TKN 节点类型。 */
%token <string> WORD
%token <string> IPV4
%token <string> IPV4_PREFIX
%token <string> IPV6
%token <string> IPV6_PREFIX
%token <string> VARIABLE
%token <string> RANGE
%token <string> MAC
%token <string> MAC_PREFIX
%token <string> ASNUM

/* special syntax, value is irrelevant */
/* 【终结符】EXCL_BRACKET:词法器对 "![" 识别出的特殊记号,用于 ![ ] 否定式可选;
 * 其字符串值无关紧要(见 !\[ 规则)。 */
%token <string> EXCL_BRACKET

/* union types for parsed rules */
/* 【非终结符类型】标注各非终结符的语义值类型:
 *   <node>     : start / literal_token / placeholder_token(_real) / simple_token
 *   <subgraph> : selector / selector_token / selector_token_seq / selector_seq_seq
 *   <string>   : varname_token
 * 这些标注让每处 $n 的类型确定,是 bison 做类型检查的依据。 */
%type <node> start
%type <node> literal_token
%type <node> placeholder_token
%type <node> placeholder_token_real
%type <node> simple_token
%type <subgraph> selector
%type <subgraph> selector_token
%type <subgraph> selector_token_seq
%type <subgraph> selector_seq_seq

%type <string> varname_token

/* 【代码块】bison 前置声明区:声明语法错误回调 cmd_yyerror(实现见文件末 yyerror)
 * 及若干静态辅助函数(doc_next / new_token_node / terminate_graph / cleanup /
 * loopcheck);并定义便捷宏 scanner = ctx->scanner。 */
%code {

  /* bison declarations */
  void
  cmd_yyerror (CMD_YYLTYPE *locp, struct parser_ctx *ctx, char const *msg);

  /* helper functions for parser */
  static const char *
  doc_next (struct parser_ctx *ctx);

  static struct graph_node *
  new_token_node (struct parser_ctx *,
                  enum cmd_token_type type,
                  const char *text,
                  const char *doc);

  static void
  terminate_graph (CMD_YYLTYPE *locp, struct parser_ctx *ctx,
                   struct graph_node *);

  static void
  cleanup (struct parser_ctx *ctx);

  static void loopcheck(struct parser_ctx *ctx, struct subgraph *sg);

  #define scanner ctx->scanner
}

/* yyparse parameters */
/* 【参数】lex-param / parse-param:把 scanner 与 parser_ctx 作为参数传给
 * yyparse / yylex,配合 api.pure 实现可重入。 */
%lex-param {yyscan_t scanner}
%parse-param {struct parser_ctx *ctx}

/* called automatically before yyparse */
/* 【初始化动作】yyparse 开始前自动执行:把 currnode 指向图的首节点,并复制一份
 * cmd_element->doc 到 ctx->docstr(末尾放哨兵 0x03 供 doc_next 判断“文档用尽”)。 */
%initial-action {
  /* clear state pointers */
  ctx->currnode = vector_slot (ctx->graph->nodes, 0);

  /* copy docstring and keep a pointer to the copy */
  if (ctx->el->doc)
  {
    // allocate a new buffer, making room for a flag
    size_t length = (size_t) strlen (ctx->el->doc) + 2;
    ctx->docstr = malloc (length);
    memcpy (ctx->docstr, ctx->el->doc, strlen (ctx->el->doc));
    // set the flag so doc_next knows when to print a warning
    ctx->docstr[length - 2] = 0x03;
    // null terminate
    ctx->docstr[length - 1] = 0x00;
  }
  ctx->docstr_start = ctx->docstr;
}

%{
#ifdef __clang__
# if __clang_major__ > 12
#  pragma GCC diagnostic ignored "-Wunused-but-set-variable"
# endif
#endif
%}

%%

/* 【起始规则】start 是整条命令定义串的顶层产生式。
 * 第一种:只有 cmd_token_seq(末尾接命令元素节点)。
 * 第二种:cmd_token_seq 后跟 placeholder_token 与 '...'(省略号),表示“可变参”:
 *   把占位符节点自连成环(graph_add_edge 自身)以接受任意个数,并置 allowrepeat;
 *   两种都以 terminate_graph 收尾,挂上 END_TKN 与 cmd_element。
 * 【易踩的坑】自环是可变参的唯一建模方式,改动此规则会直接影响 '...' 语义。 */
start:
  cmd_token_seq
{
  // tack on the command element
  terminate_graph (&@1, ctx, ctx->currnode);
}
| cmd_token_seq placeholder_token '.' '.' '.'
{
  if ((ctx->currnode = graph_add_edge (ctx->currnode, $2)) != $2)
    graph_delete_node (ctx->graph, $2);

  ((struct cmd_token *)ctx->currnode->data)->allowrepeat = 1;

  // adding a node as a child of itself accepts any number
  // of the same token, which is what we want for variadics
  graph_add_edge (ctx->currnode, ctx->currnode);

  // tack on the command element
  terminate_graph (&@1, ctx, ctx->currnode);
}
;

/* 【规则】varname_token:'$' WORD 或空。
 * 作用:可选地给前一 token 命名(如 "$name"),名字用于生成 C 里访问该段的变量名;
 * 无关时可以为空($$ = NULL)。 */
varname_token: '$' WORD
{
  $$ = $2;
}
| /* empty */
{
  $$ = NULL;
}
;

/* 【规则】cmd_token_seq:一个 cmd_token 序列(可空)。
 * 左递归逐个拼接,构成命令主体。 */
cmd_token_seq:
  /* empty */
| cmd_token_seq cmd_token
;

/* 【规则】cmd_token:命令里的一个单元,要么 simple_token,要么 selector。
 * simple_token 直接挂到当前节点后;selector 把子图接上,并把 currnode 推进到子图
 * 末端。graph_add_edge 若返回的节点不是 $1(说明已存在同边、复用了旧节点),
 * 则释放新建的重复节点。 */
cmd_token:
  simple_token
{
  if ((ctx->currnode = graph_add_edge (ctx->currnode, $1)) != $1)
    graph_delete_node (ctx->graph, $1);
  cmd_token_varname_seqappend($1);
}
| selector
{
  graph_add_edge (ctx->currnode, $1.start);
  cmd_token_varname_seqappend($1.start);
  ctx->currnode = $1.end;
}
;

/* 【规则】simple_token:字面量 token 或占位符 token(不含选择 / 可选结构)。 */
simple_token:
  literal_token
| placeholder_token
;

/* 【规则】literal_token:WORD + 可选变量名。
 * 生成 WORD_TKN 节点,文档串由 doc_next(ctx) 顺序取出(每个 token 对应文档的一行);
 * 若带变量名则写入 token;最后释放词法器分配的字符串。
 * 【易踩的坑】doc_next 与 token 必须一一对应,doc 行数不足会被记录为错误位。 */
literal_token: WORD varname_token
{
  $$ = new_token_node (ctx, WORD_TKN, $1, doc_next(ctx));
  cmd_token_varname_set ($$->data, $2);
  XFREE (MTYPE_LEX, $2);
  XFREE (MTYPE_LEX, $1);
}
;

/* 【规则】placeholder_token_real:各类占位符的产生式集合。
 * 每个备选把对应词法 token(IPV4 / IPV6 / MAC / ... / RANGE / ASNUM)转成相应
 * *_TKN 节点。其中 RANGE 额外解析上下界:跳过 '(',用 strtoll 取 min、strsep('-')
 * 取 max,并校验 min<=max,否则报 "Invalid range."。
 * 【易踩的坑】RANGE 分支直接改写了 yylval.string 指针,读时注意它已被前移。 */
placeholder_token_real:
  IPV4
{
  $$ = new_token_node (ctx, IPV4_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| IPV4_PREFIX
{
  $$ = new_token_node (ctx, IPV4_PREFIX_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| IPV6
{
  $$ = new_token_node (ctx, IPV6_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| IPV6_PREFIX
{
  $$ = new_token_node (ctx, IPV6_PREFIX_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| VARIABLE
{
  $$ = new_token_node (ctx, VARIABLE_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| RANGE
{
  $$ = new_token_node (ctx, RANGE_TKN, $1, doc_next(ctx));
  struct cmd_token *token = $$->data;

  // get the numbers out
  yylval.string++;
  token->min = strtoll (yylval.string, &yylval.string, 10);
  strsep (&yylval.string, "-");
  token->max = strtoll (yylval.string, &yylval.string, 10);

  // validate range
  if (token->min > token->max) cmd_yyerror (&@1, ctx, "Invalid range.");

  XFREE (MTYPE_LEX, $1);
}
| MAC
{
  $$ = new_token_node (ctx, MAC_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| MAC_PREFIX
{
  $$ = new_token_node (ctx, MAC_PREFIX_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}
| ASNUM
{
  $$ = new_token_node (ctx, ASNUM_TKN, $1, doc_next(ctx));
  XFREE (MTYPE_LEX, $1);
}

/* 【规则】placeholder_token:占位符 + 可选变量名(设置 varname)。 */
placeholder_token:
  placeholder_token_real varname_token
{
  $$ = $1;
  cmd_token_varname_set ($$->data, $2);
  XFREE (MTYPE_LEX, $2);
};


/* <selector|set> productions */
/* 【规则】selector:尖括号 <...> 表示“任选其一”。
 * 直接沿用内部 seq 的子图,并把变量名 join 到末端。 */
selector: '<' selector_seq_seq '>' varname_token
{
  $$ = $2;
  cmd_token_varname_join ($2.end, $4);
  XFREE (MTYPE_LEX, $4);
};

/* 【规则】selector_seq_seq:用 '|' 连接的备选序列。
 * 每个备选前插入 FORK_TKN、后插入 JOIN_TKN 并互指(forkjoin),
 * 形成“分叉—汇合”结构,从而把并列分支并入同一张图。 */
selector_seq_seq:
  selector_seq_seq '|' selector_token_seq
{
  $$ = $1;
  graph_add_edge ($$.start, $3.start);
  graph_add_edge ($3.end, $$.end);
}
| selector_token_seq
{
  $$.start = new_token_node (ctx, FORK_TKN, NULL, NULL);
  $$.end   = new_token_node (ctx, JOIN_TKN, NULL, NULL);
  ((struct cmd_token *)$$.start->data)->forkjoin = $$.end;
  ((struct cmd_token *)$$.end->data)->forkjoin = $$.start;

  graph_add_edge ($$.start, $1.start);
  graph_add_edge ($1.end, $$.end);
}
;

/* {keyword} productions */
/* 【规则】selector:{...} 表示“至少选一个”。
 * 与 <> 的关键差异:额外加一条 end->start 的边(可绕回),且刻意不加 start->end,
 * 以获得“至少一个”的语义并避免给 loopcheck 引入难处理的空路径;
 * 若确实想要“零或一个”请写 [{...}]。本规则调用 loopcheck 检测空路径。 */
selector: '{' selector_seq_seq '}' varname_token
{
  $$ = $2;
  graph_add_edge ($$.end, $$.start);
  /* there is intentionally no start->end link, for two reasons:
   * 1) this allows "at least 1 of" semantics, which are otherwise impossible
   * 2) this would add a start->end->start loop in the graph that the current
   *    loop-avoidal fails to handle
   * just use [{a|b}] if necessary, that will work perfectly fine, and reason
   * #1 is good enough to keep it this way. */

  loopcheck(ctx, &$$);
  cmd_token_varname_join ($2.end, $4);
  XFREE (MTYPE_LEX, $4);
};


/* 【规则】selector_token:选择结构里的一个单元;simple_token 时起止节点相同,
 * 否则是一个嵌套 selector(子图)。 */
selector_token:
  simple_token
{
  $$.start = $$.end = $1;
}
| selector
;

/* 【规则】selector_token_seq:selector_token 的串联;依次连边并串接变量名,
 * 返回首尾构成的子图。 */
selector_token_seq:
  selector_token_seq selector_token
{
  graph_add_edge ($1.end, $2.start);
  cmd_token_varname_seqappend($2.start);
  $$.start = $1.start;
  $$.end   = $2.end;
}
| selector_token
;

/* [option] productions */
/* 【规则】selector:[...] 表示“可选(零或一)”。
 * 沿用子图,并在 start 与 end 间加一条直连边,表示“可跳过”。 */
selector: '[' selector_seq_seq ']' varname_token
{
  $$ = $2;
  graph_add_edge ($$.start, $$.end);
  cmd_token_varname_join ($2.end, $4);
  XFREE (MTYPE_LEX, $4);
}
;

/* ![option] productions */
/* 【规则】selector:![...] 表示“否定式可选”(NEG_ONLY)。
 * 在 start 与 end 之间插入一个 NEG_ONLY_TKN 节点,表示“若出现则为否定匹配”。 */
selector: EXCL_BRACKET selector_seq_seq ']' varname_token
{
  struct graph_node *neg_only = new_token_node (ctx, NEG_ONLY_TKN, NULL, NULL);

  $$ = $2;
  graph_add_edge ($$.start, neg_only);
  graph_add_edge (neg_only, $$.end);
  cmd_token_varname_join ($2.end, $4);
  XFREE (MTYPE_LEX, $4);
}
;

%%

/* 【分隔】%% 结束语法规则段,以下为配套 C 代码。
 * #undef scanner:取消 %code 里为便捷而定义的 scanner 宏,此后需写 ctx->scanner。 */
#undef scanner

/* 【内存类型】为词法器临时字符串注册内存类型 LEX("Lexer token (temporary)")。 */
DEFINE_MTYPE(LIB, LEX, "Lexer token (temporary)");

/* 【入口】把一条命令定义串编译成命令图——本文件对外的主入口。
 * 参数 graph : 待填充的图(调用方已含起点节点)。
 * 参数 cmd   : 命令定义(含 string / doc / attr)。
 * 返回：位标志 retval(0 成功;否则含 CMD_GRAPH_PARSE_DOCSTRING_* 等错误位)。
 * 逻辑：初始化 parser_ctx -> 设置词法器输入串 -> cmd_yyparse 解析 -> 清理词法器与
 *   上下文。
 * 副作用：就地修改 graph;可能经 yyerror 打印解析错误。
 * 【易踩的坑】返回 0 才表示完全成功;文档串不匹配只记标志位、不视为致命。 */
uint32_t cmd_graph_parse(struct graph *graph, const struct cmd_element *cmd)
{
  struct parser_ctx ctx = { .graph = graph, .el = cmd, .retval = 0 };

  // set to 1 to enable parser traces
  yydebug = 0;

  set_lexer_string (&ctx.scanner, cmd->string);

  // parse command into DFA
  cmd_yyparse (&ctx);

  /* cleanup lexer */
  cleanup_lexer (&ctx.scanner);

  // cleanup
  cleanup (&ctx);

  return ctx.retval;
}

/* parser helper functions */

/* 【辅助】深度优先检测子图内是否存在(在 start 与 end 之间可绕回的)空路径 / 回路。
 * 参数 start / node / end : 搜索起点、当前节点、目标终点。
 * 参数 depth : 递归深度(上限 64,达上限即判 true,避免爆栈)。
 * 返回：发现回路 / 空路径为 true。
 * 说明：只沿“特殊 token”(type >= SPECIAL_TKN,即 FORK / JOIN 等结构节点)深入。 */
static bool loopcheck_inner(struct graph_node *start, struct graph_node *node,
			    struct graph_node *end, size_t depth)
{
	size_t i;
	bool ret;

	/* safety check */
	if (depth++ == 64)
		return true;

	for (i = 0; i < vector_active(node->to); i++) {
		struct graph_node *next = vector_slot(node->to, i);
		struct cmd_token *tok = next->data;

		if (next == end || next == start)
			return true;
		if (tok->type < SPECIAL_TKN)
			continue;
		ret = loopcheck_inner(start, next, end, depth);
		if (ret)
			return true;
	}
	return false;
}

/* 【辅助】对 {} 子图调用 loopcheck_inner,若发现空路径则记 FATAL 日志,
 * 提示改用 [{...}] 写法。 */
static void loopcheck(struct parser_ctx *ctx, struct subgraph *sg)
{
	if (loopcheck_inner(sg->start, sg->start, sg->end, 0))
		zlog_err("FATAL: '%s': {} contains an empty path! Use [{...}]",
			 ctx->el->string);
}

void
/* 【错误报告】bison 语法错误回调(对应声明里的 cmd_yyerror,这里实现为 yyerror)。
 * 参数 loc / ctx / msg : 位置、解析上下文、错误信息。
 * 返回：无。副作用：用 zlog_notice 打印出错行及列指示(用 '^' 与空格拼出的箭头)。
 * 【易踩的坑】把整条定义串按行打印并把错误列标出便于定位;spacing 缓冲固定 256。 */
yyerror (CMD_YYLTYPE *loc, struct parser_ctx *ctx, char const *msg)
{
  char *tmpstr = strdup(ctx->el->string);
  char *line, *eol;
  char spacing[256];
  int lineno = 0;

  zlog_notice ("%s: FATAL parse error: %s", __func__, msg);
  zlog_notice ("%s: %d:%d-%d of this command definition:", __func__, loc->first_line, loc->first_column, loc->last_column);

  line = tmpstr;
  do {
    lineno++;
    eol = strchr(line, '\n');
    if (eol)
      *eol++ = '\0';

    zlog_notice ("%s: | %s", __func__, line);
    if (lineno == loc->first_line && lineno == loc->last_line
        && loc->first_column < (int)sizeof(spacing) - 1
        && loc->last_column < (int)sizeof(spacing) - 1) {

      int len = loc->last_column - loc->first_column;
      if (len == 0)
        len = 1;

      memset(spacing, ' ', loc->first_column - 1);
      memset(spacing + loc->first_column - 1, '^', len);
      spacing[loc->first_column - 1 + len] = '\0';
      zlog_notice ("%s: | %s", __func__, spacing);
    }
  } while ((line = eol));
  free(tmpstr);
}

static void
/* 【辅助】释放解析上下文占用的资源(docstr 副本)并清空状态指针。 */
cleanup (struct parser_ctx *ctx)
{
  /* free resources */
  free (ctx->docstr_start);

  /* clear state pointers */
  ctx->currnode = NULL;
  ctx->docstr_start = ctx->docstr = NULL;
}

static void
/* 【辅助】为解析完成的图收尾:追加 END_TKN 节点与 cmd_element 终点节点。
 * 参数 finalnode : 命令主体最后一个节点。
 * 副作用：若仍有剩余文档串(strlen>1)说明 docstring 给多了,打印告警并置
 *   CMD_GRAPH_PARSE_DOCSTRING_EXTRA 标志。
 * 目标结构:... -> finalnode -> END_TKN -> cmd_element。 */
terminate_graph (CMD_YYLTYPE *locp, struct parser_ctx *ctx,
                 struct graph_node *finalnode)
{
  // end of graph should look like this
  // * -> finalnode -> END_TKN -> cmd_element
  const struct cmd_element *element = ctx->el;
  struct graph_node *end_token_node =
    new_token_node (ctx, END_TKN, CMD_CR_TEXT, "");
  struct graph_node *end_element_node =
    graph_new_node (ctx->graph, (void *)element, NULL);

  if (ctx->docstr && strlen (ctx->docstr) > 1) {
    zlog_err ("Excessive docstring while parsing '%s'", ctx->el->string);
    zlog_err ("----------");
    while (ctx->docstr && ctx->docstr[1] != '\0')
      zlog_err ("%s", strsep(&ctx->docstr, "\n"));
    zlog_err ("----------");
    ctx->retval |= CMD_GRAPH_PARSE_DOCSTRING_EXTRA;
  }

  graph_add_edge (finalnode, end_token_node);
  graph_add_edge (end_token_node, end_element_node);
}

static const char *
/* 【辅助】顺序取出下一条文档串(每个 token 用一行 doc)。
 * 返回：当前行(以 '\n' 切分);若读到的首字符是哨兵 0x03 说明 doc 行数不够,
 *   则打印告警、返回空串并置 CMD_GRAPH_PARSE_DOCSTRING_MISSING 标志。 */
doc_next (struct parser_ctx *ctx)
{
  const char *piece = ctx->docstr ? strsep (&ctx->docstr, "\n") : "";
  if (*piece == 0x03)
  {
    zlog_err ("Ran out of docstring while parsing '%s'", ctx->el->string);
    piece = "";
    ctx->retval |= CMD_GRAPH_PARSE_DOCSTRING_MISSING;
  }

  return piece;
}

static struct graph_node *
/* 【辅助】新建一个 cmd_token 并作为节点挂到图上。
 * 参数 type / text / doc : token 类型、显示文本、文档;attr 取自 ctx->el->attr。
 * 返回：新图节点;其析构函数为 cmd_token_del(随图释放 token)。 */
new_token_node (struct parser_ctx *ctx, enum cmd_token_type type,
                const char *text, const char *doc)
{
  struct cmd_token *token = cmd_token_new (type, ctx->el->attr, text, doc);
  return graph_new_node (ctx->graph, token, (void (*)(void *)) &cmd_token_del);
}
