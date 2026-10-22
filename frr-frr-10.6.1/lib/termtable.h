// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ASCII table generator.
 * Copyright (C) 2017  Cumulus Networks
 * Quentin Young
 */
/*
 * 文件导读：termtable.h —— 终端 ASCII 表格生成接口
 *
 * 一、这个文件做什么
 *   声明一套在终端里渲染对齐表格的工具：把若干行列的文本画成带边框的表格，
 *   供各模块的 show 命令输出使用（例如 show ip route、show bgp summary 等）。
 *   实现见 lib/termtable.c。
 *
 * 二、核心数据结构
 *   - ttable：整张表，含行数 / 列数、二维单元格数组与样式。
 *   - ttable_cell：一个单元格，含文本与单元格样式。
 *   - ttable_cellstyle：单元格样式（左右内边距、对齐方式、边框）。
 *   - ttable_style：整表样式（角字符、缩进、边框与默认单元格样式）。
 *   - ttable_border：四条边用哪个字符、是否开启。
 *   - ttable_align：对齐枚举 LEFT / RIGHT / TOP / BOTTOM。
 *
 * 三、常用流程
 *   ttable_new 建表 -> 反复 ttable_add_row 追加行（格式串用竖线分隔各列）
 *   -> ttable_align / ttable_pad / ttable_restyle 调整 -> ttable_dump 取文本，
 *   或 ttable_json 取 json；最后 ttable_del 释放。
 *
 * 四、易踩的坑
 *   - 行格式串里竖线的个数必须正好等于 列数减 1，否则该行不会被创建。
 *   - 目前单元格内容里不能再嵌入竖线。
 *   - ttable_dump / ttable_json 返回的对象需调用方自行释放。
 *
 * 五、阅读顺序
 *   先看枚举与各结构体，再看建表 / 增行，最后看对齐、内边距与输出接口。
 */

#ifndef _TERMTABLE_H_
#define _TERMTABLE_H_

#include <zebra.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ttable_align — 对齐方式枚举。
 * LEFT / RIGHT：单元格内容的水平对齐。
 * TOP / BOTTOM：用于选择分隔线所在的一侧（列分隔线的左 / 右、行分隔线的上 / 下）。
 */
enum ttable_align {
	LEFT,
	RIGHT,
	TOP,
	BOTTOM,
};

/*
 * ttable_border — 表格 / 单元格边框描述。
 * top / bottom / left / right：四条边使用的字符。
 * 相应的 *_on 字段：该边是否启用（false 则不绘制）。
 */
struct ttable_border {
	char top;
	char bottom;
	char left;
	char right;

	bool top_on;
	bool bottom_on;
	bool left_on;
	bool right_on;
};

/*
 * ttable_cellstyle — 单元格样式。
 * lpad / rpad：左 / 右内边距（空格数）。
 * align：内容对齐方式。
 * border：本单元格的边框字符与开关。
 */
/* cell style and cell */
struct ttable_cellstyle {
	short lpad;
	short rpad;
	enum ttable_align align;
	struct ttable_border border;
};

/*
 * ttable_cell — 单个单元格。
 * text：单元格文本（堆分配）；style：单元格样式。
 */
struct ttable_cell {
	char *text;
	struct ttable_cellstyle style;
};

/*
 * ttable_style — 整表样式。
 * corner：交叉处的角字符；indent：整表左缩进；rownums_on：是否显示行号（当前未实现）。
 * border / cell：默认边框与默认单元格样式。
 */
/* table style and table */
struct ttable_style {
	char corner;     /* intersection */
	int indent;      /* left table indent */
	bool rownums_on; /* show row numbers; unimplemented */

	struct ttable_border border;
	struct ttable_cellstyle cell;
};

/*
 * ttable — 一张表格。
 * nrows / ncols：行数 / 列数。
 * table：单元格二维数组（按行优先，table[row][col]）。
 * size：当前分配大小；style：整表样式。
 */
struct ttable {
	int nrows;		    /* number of rows */
	int ncols;		    /* number of cols */
	struct ttable_cell **table; /* table, row x col */
	size_t size;		    /* size */
	struct ttable_style style;  /* style */
};

/*
 * 预定义样式编号：TTSTYLE_ASCII 为带边框的 ASCII 风格，TTSTYLE_BLANK 为无边框
 * 的空白风格。用作 ttable_styles 数组的下标。
 */
/* some predefined styles */
#define TTSTYLE_ASCII 0
#define TTSTYLE_BLANK 1

/*
 * ttable_styles — 预定义样式数组，下标分别为 TTSTYLE_ASCII 与 TTSTYLE_BLANK。
 */
extern const struct ttable_style ttable_styles[2];

/*
 * ttable_new — 用指定样式新建一张空表（tts 传 NULL 则用默认 ASCII 样式）。
 * 返回新表；调用方最终须用 ttable_del 释放。
 */
/**
 * Creates a new table with the default style, which looks like this:
 *
 * +----------+----------+
 * | column 1 | column 2 |
 * +----------+----------+
 * | data...  | data!!   |
 * +----------+----------+
 * | datums   | 12345    |
 * +----------+----------+
 *
 * @return the created table
 */
struct ttable *ttable_new(const struct ttable_style *tts);

/*
 * ttable_del — 销毁表格并释放所有相关资源（含每个单元格的文本）。
 */
/**
 * Deletes a table and releases all associated resources.
 *
 * @param tt the table to destroy
 */
void ttable_del(struct ttable *tt);

/*
 * ttable_insert_row — 在指定行号处插入一行，行内容由格式串给出。
 * tt：目标表；row：行号（从 0 起）；format：以竖线分隔各列的 printf 风格串。
 * 返回新行第一个单元格的指针；若竖线个数不等于 列数减 1（列数不匹配）返回 NULL。
 * 注意：目前单元格文本内不能再嵌入竖线。
 */
/**
 * Inserts a new row at the given index.
 *
 * The row contents are determined by a format string. The format string has
 * the same form as a regular printf format string, except that columns are
 * delimited by '|'. For example, to make the first column of the table above,
 * the call is:
 *
 *   ttable_insert_row(<tt>, <n>, "%s|%s", "column 1", "column 2");
 *
 * All features of printf format strings are permissible here.
 *
 * Caveats:
 *  - At present you cannot insert '|' into a cell's contents.
 *  - If there are N columns, '|' must appear n-1 times or the row will not be
 *    created
 *
 * @param tt table to insert row into
 * @param row the row number (begins at 0)
 * @param format column-separated format string
 * @param ... arguments to format string
 *
 * @return pointer to the first cell in the created row, or NULL if not enough
 * columns were specified
 */
struct ttable_cell *ttable_insert_row(struct ttable *tt, unsigned int row,
				      const char *format, ...) PRINTFRR(3, 4);
/*
 * ttable_add_row — 在表尾追加一行，语义同 ttable_insert_row。
 * tt：目标表；format：以竖线分隔各列的格式串。返回新行第一个单元格指针；
 * 列数不足时返回 NULL（表列为 0 时直接失败）。
 */
/**
 * Inserts a new row at the end of the table.
 *
 * The row contents are determined by a format string. The format string has
 * the same form as a regular printf format string, except that columns are
 * delimited by '|'. For example, to make the first column of the table above,
 * the call is:
 *
 *   ttable_add_row(<tt>, "%s|%s", "column 1", "column 2");
 *
 * All features of printf format strings are permissible here.
 *
 * Caveats:
 *  - At present you cannot insert '|' into a cell's contents.
 *  - If there are N columns, '|' must appear n-1 times or the row will not be
 *    created
 *
 * @param tt table to insert row into
 * @param format column-separated format string
 * @param ... arguments to format string
 *
 * @return pointer to the first cell in the created row, or NULL if not enough
 * columns were specified
 */
struct ttable_cell *ttable_add_row(struct ttable *tt, const char *format, ...)
	PRINTFRR(2, 3);

/*
 * ttable_del_row — 删除指定行号（从 0 起）的一行。
 */
/**
 * Removes a row from the table.
 *
 * @param tt table to delete row from
 * @param row the row number (begins at 0)
 */
void ttable_del_row(struct ttable *tt, unsigned int row);

/*
 * ttable_align — 设置一个矩形区域内单元格的对齐方式。
 * tt：目标表；srow / scol：起始行 / 列；erow / ecol：结束行 / 列；
 * align：LEFT 或 RIGHT。注意：对齐在保留内边距的前提下生效。
 */
/**
 * Sets alignment for a range of cells.
 *
 * Available alignments are LEFT and RIGHT. Cell contents will be aligned
 * accordingly, while respecting padding (if any). Suppose a cell has:
 *
 * lpad = 1
 * rpad = 1
 * align = RIGHT
 * text = 'datums'
 *
 * The cell would look like:
 *
 *  +-------------------+
 *  |            datums |
 *  +-------------------+
 *
 * On the other hand:
 *
 * lpad = 1
 * rpad = 10
 * align = RIGHT
 * text = 'datums'
 *
 *  +-------------------+
 *  |   datums          |
 *  +-------------------+
 *
 * The default alignment is LEFT.
 *
 * @param tt the table to set alignment on
 * @param srow starting row index
 * @param scol starting column index
 * @param nrow # rows to align
 * @param ncol # cols to align
 * @param align the alignment to set
 */
void ttable_align(struct ttable *tt, unsigned int srow, unsigned int scol,
		  unsigned int erow, unsigned int ecol,
		  enum ttable_align align);

/*
 * ttable_pad — 为一个矩形区域内单元格设置内边距。
 * tt：目标表；srow / scol：起始行 / 列；nrow / ncol：行数 / 列数；
 * align：LEFT 或 RIGHT（复用对齐枚举表示给哪侧加内边距）；pad：空格个数。
 */
/**
 * Sets padding for a range of cells.
 *
 * Available padding options are LEFT and RIGHT (the alignment enum is reused).
 * Both options may be set. Padding is treated as though it is stuck to the
 * walls of the cell. Suppose a cell has:
 *
 * lpad = 4
 * rpad = 2
 * align = RIGHT
 * text = 'datums'
 *
 * The cell padding, marked by '.', would look like:
 *
 *  +--------------+
 *  |    .datums.  |
 *  +--------------+
 *
 * If the column is wider than the cell, the cell contents are aligned in an
 * additional padded field according to the cell alignment.
 *
 *  +--------------------+
 *  | Data!!!11!~~~~~:-) |
 *  +--------------------+
 *  |    .      datums.  |
 *  +--------------------+
 *
 * @param tt the table to set padding on
 * @param srow starting row index
 * @param scol starting column index
 * @param nrow # rows to pad
 * @param ncol # cols to pad
 * @param align LEFT or RIGHT
 * @param pad # spaces to pad with
 */
void ttable_pad(struct ttable *tt, unsigned int srow, unsigned int scol,
		unsigned int nrow, unsigned int ncol, enum ttable_align align,
		short pad);

/*
 * ttable_restyle — 按表的默认单元格样式（ttable_style.cell）重排所有单元格样式。
 */
/**
 * Restyle all cells according to table.cell.style.
 *
 * @param tt table to restyle
 */
void ttable_restyle(struct ttable *tt);

/*
 * ttable_colseps — 开关指定列的左 / 右侧列分隔线。
 * tt：目标表；col：列号；align：LEFT 或 RIGHT（选哪侧分隔线）；on：开 / 关；
 * sep：分隔线使用的字符。
 */
/**
 * Turn left/right column separators on or off for specified column.
 *
 * @param tt table
 * @param col column index
 * @param align left or right separators
 * @param on true/false for on/off
 * @param sep character to use
 */
void ttable_colseps(struct ttable *tt, unsigned int col,
		    enum ttable_align align, bool on, char sep);

/*
 * ttable_rowseps — 开关指定行的下边框（行分隔线）。
 * tt：目标表；row：行号；align：LEFT 或 RIGHT（选哪侧）；on：开 / 关；sep：字符。
 */
/**
 * Turn bottom row separators on or off for specified row.
 *
 * @param tt table
 * @param row row index
 * @param align left or right separators
 * @param on true/false for on/off
 * @param sep character to use
 */
void ttable_rowseps(struct ttable *tt, unsigned int row,
		    enum ttable_align align, bool on, char sep);

/*
 * ttable_dump — 把表格渲染成堆分配的字符串，newline 指定换行序列（须以 NULL 结尾）。
 * 返回文本，调用方需用 XFREE(MTYPE_TMP_TTABLE, str) 释放。
 */
/**
 * Dumps a table to a heap-allocated string.
 *
 * Caller must free this string after use with
 *
 *   XFREE (MTYPE_TMP_TTABLE, str);
 *
 * @param tt the table to dump
 * @param newline the desired newline sequence to use, null terminated.
 * @return table in text form
 */
char *ttable_dump(struct ttable *tt, const char *newline);

/**
 * Convert a table to a JSON array of objects.
 *
 * Caller must free the returned json_object structure.
 *
 * @param tt the table to convert
 * @param formats an array of characters indicating what JSON type should be
 * used.
 */
struct json_object;
/*
 * ttable_json — 把表格转成 json 对象数组；formats 为逐列的目标 JSON 类型标识串。
 * 返回 json_object，调用方需自行释放。
 */
struct json_object *ttable_json(struct ttable *tt, const char *const formats);

/**
 * Convert a table to a JSON array of objects.
 *
 * Caller must free the returned json_object structure.
 *
 * @param tt the table to convert
 * @param formats an array of characters indicating what JSON type should be
 * used.
 * @param formats an optinal string of row headers that overrids the first row of the table.
 * This is useful to get naming convention that align with caml Format.
 */
/*
 * ttable_json_with_json_text — 同 ttable_json，但可用 json_override_text 覆盖
 * 表头（首行）的键名，便于对齐 caml Format 等命名约定。返回的 json 由调用方释放。
 */
struct json_object *ttable_json_with_json_text(struct ttable *tt,
					       const char *const formats,
					       const char *json_override_text);

#ifdef __cplusplus
}
#endif

#endif /* _TERMTABLE_H */
