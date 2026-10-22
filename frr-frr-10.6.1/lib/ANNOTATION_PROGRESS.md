# lib 源码注释进度表

分支: study/bgpd-annotated（与 bgpd 注释同一分支，未新建分支） | 注释语言: 中文 | 粒度: 函数级 + 关键行/数据结构
规则: 只增注释, 不改任何代码 (每文件用 git diff 校验: 删除行必须为 0)

## 适用范围
- 顶层 282 个 .c/.h
- printf/（3 个 .c + 2 个 .h）、assert/（1 个 .h）
- 非 C 文件：command_parse.y、command_lex.l、defun_lex.l、northbound_grpc.cpp
- 合计 292 个文件 / 约 137,555 行
- 不含：Makefile、subdir.am、*.pl、route_types.txt、config_paths.h.in、version.h.in

## 校验方式（三层，每完成一个文件都要跑）
1. 新增/删除行: `git diff --numstat -- <file>` 第二列必须为 0
2. 新增行必须全是注释:
   `git diff -U0 -- <file> | grep -E '^\+' | grep -v '^+++' | grep -vE '^\+[[:space:]]*(/\*|\*|//)'`
   必须为空(允许极少数空行)
3. **最强校验(推荐每次都跑)**: 剥离注释后比较代码流
   `perl "C:/Users/ASUS/AppData/Local/Temp/bgpd_annot_check.pl" D:/share/runos/frr-frr-10.6.1 <git 相对路径>`
   它把 HEAD 版与工作版都做“去掉注释后的代码行比较”，输出 `OK` 才算通过。
   (辅助诊断脚本: `C:/Users/ASUS/AppData/Local/Temp/bgpd_leakscan.pl`)

## 该比较器在 lex/yacc 文件上的已知失效点
`defun_lex.l` 第 99 行有一条词法规则 `"/*"`（匹配 C 注释开头的**字符串字面量**）。
比较器的状态机会把它当成真的注释开始，从而报 `unterminated comment` —— 这是**误报**。
所以 `command_parse.y` / `command_lex.l` / `defun_lex.l` / `northbound_grpc.cpp` 改用:
- 第 1 层（删除数必须为 0）+ 第 2 层（新增行必须全是注释）+
- 人工 `git diff` 通读一遍。

## 注释写作禁忌
- 中文注释正文中**禁止出现 `*/` 或 `/*`**。要表达“A 和 B 两类”请写成 `A / B`(斜杠两侧留空格)。
- 不要把中文块插在已有英文注释块**内部**(锚点若在注释块中间，块尾的 `*/` 会提前闭合它)。
  正确做法: 中文块单独起一个 `/* ... */`，英文原注释块保持一字不动(包括它自己的 `/*` 起始行)。
- 已有英文注释行**一行都不能改**（包括横幅的首行）。改了它 `git diff --numstat` 的删除数就不为 0。

## 说明
- “行数”列是**当前工作区**的行数(即注释后的行数)，不是原始行数。
- 状态: ✅ 完成 = 已注释且通过校验; 🔄 进行中 = 已派发但尚未交付/尚未校验; ⬜ 待做 = 未开始

| 状态 | 行数 | 文件 |
|------|------|------|
| | | **顶层** |
| ✅ 完成 | 537 | admin_group.c |
| ✅ 完成 | 125 | admin_group.h |
| ✅ 完成 | 179 | affinitymap.c |
| ✅ 完成 | 124 | affinitymap.h |
| ✅ 完成 | 133 | affinitymap_cli.c |
| ✅ 完成 | 147 | affinitymap_northbound.c |
| ✅ 完成 | 523 | agentx.c |
| ✅ 完成 | 96 | agg_table.c |
| ✅ 完成 | 239 | agg_table.h |
| ✅ 完成 | 346 | asn.c |
| ✅ 完成 | 132 | asn.h |
| ✅ 完成 | 427 | atomlist.c |
| ✅ 完成 | 539 | atomlist.h |
| ✅ 完成 | 264 | base64.c |
| ✅ 完成 | 127 | base64.h |
| ✅ 完成 | 1646 | bfd.c |
| ✅ 完成 | 676 | bfd.h |
| ✅ 完成 | 329 | bitfield.h |
| ✅ 完成 | 596 | buffer.c |
| ✅ 完成 | 191 | buffer.h |
| ✅ 完成 | 203 | checksum.c |
| ✅ 完成 | 140 | checksum.h |
| ✅ 完成 | 225 | clippy.c |
| ✅ 完成 | 55 | clippy.h |
| ✅ 完成 | 3150 | command.c |
| ✅ 完成 | 815 | command.h |
| ✅ 完成 | 723 | command_graph.c |
| ✅ 完成 | 245 | command_graph.h |
| ✅ 完成 | 1230 | command_match.c |
| ✅ 完成 | 142 | command_match.h |
| ✅ 完成 | 697 | command_py.c |
| ✅ 完成 | 578 | compiler.h |
| ✅ 完成 | 815 | cspf.c |
| ✅ 完成 | 329 | cspf.h |
| ✅ 完成 | 899 | csv.c |
| ✅ 完成 | 276 | csv.h |
| ✅ 完成 | 327 | darr.c |
| ✅ 完成 | 1164 | darr.h |
| ✅ 完成 | 394 | db.c |
| ✅ 完成 | 82 | db.h |
| ✅ 完成 | 175 | debug.c |
| ✅ 完成 | 322 | debug.h |
| ✅ 完成 | 322 | defaults.c |
| ✅ 完成 | 238 | defaults.h |
| ✅ 完成 | 736 | distribute.c |
| ✅ 完成 | 186 | distribute.h |
| ✅ 完成 | 1544 | elf_py.c |
| ✅ 完成 | 3289 | event.c |
| ✅ 完成 | 83 | explicit_bzero.c |
| ✅ 完成 | 403 | ferr.c |
| ✅ 完成 | 373 | ferr.h |
| ✅ 完成 | 1449 | filter.c |
| ✅ 完成 | 428 | filter.h |
| ✅ 完成 | 2360 | filter_cli.c |
| ✅ 完成 | 2844 | filter_nb.c |
| ✅ 完成 | 277 | flex_algo.c |
| ✅ 完成 | 226 | flex_algo.h |
| ✅ 完成 | 1060 | freebsd-queue.h |
| ✅ 完成 | 458 | frr_pthread.c |
| ✅ 完成 | 363 | frr_pthread.h |
| ✅ 完成 | 428 | frr_zmq.c |
| ✅ 完成 | 213 | frr_zmq.h |
| ✅ 完成 | 58 | frratomic.h |
| ✅ 完成 | 657 | frrcu.c |
| ✅ 完成 | 264 | frrcu.h |
| ✅ 完成 | 57 | frrdistance.h |
| ✅ 完成 | 58 | frregex_real.h |
| ✅ 完成 | 411 | frrevent.h |
| ✅ 完成 | 706 | frrlua.c |
| ✅ 完成 | 317 | frrlua.h |
| ✅ 完成 | 564 | frrscript.c |
| ✅ 完成 | 473 | frrscript.h |
| ✅ 完成 | 67 | frrsendmmsg.h |
| ✅ 完成 | 327 | frrstr.c |
| ✅ 完成 | 221 | frrstr.h |
| ✅ 完成 | 667 | grammar_sandbox.c |
| ✅ 完成 | 114 | grammar_sandbox_main.c |
| ✅ 完成 | 344 | graph.c |
| ✅ 完成 | 230 | graph.h |
| ✅ 完成 | 580 | hash.c |
| ✅ 完成 | 483 | hash.h |
| ✅ 完成 | 125 | hook.c |
| ✅ 完成 | 364 | hook.h |
| ✅ 完成 | 109 | host_cli.c |
| ✅ 完成 | 73 | host_nb.c |
| ✅ 完成 | 30 | host_nb.h |
| ✅ 完成 | 170 | iana_afi.h |
| ✅ 完成 | 534 | id_alloc.c |
| ✅ 完成 | 169 | id_alloc.h |
| ✅ 完成 | 2429 | if.c |
| ✅ 完成 | 997 | if.h |
| ✅ 完成 | 453 | if_rmap.c |
| ✅ 完成 | 155 | if_rmap.h |
| ✅ 完成 | 276 | ipaddr.h |
| ✅ 完成 | 188 | iso.c |
| ✅ 完成 | 93 | iso.h |
| ✅ 完成 | 257 | jhash.c |
| ✅ 完成 | 86 | jhash.h |
| ✅ 完成 | 899 | json.c |
| ✅ 完成 | 303 | json.h |
| ✅ 完成 | 464 | keychain.c |
| ✅ 完成 | 231 | keychain.h |
| ✅ 完成 | 1232 | keychain_cli.c |
| ✅ 完成 | 1149 | keychain_nb.c |
| ✅ 完成 | 121 | ldp_sync.c |
| ✅ 完成 | 153 | ldp_sync.h |
| ✅ 完成 | 405 | lib_errors.c |
| ✅ 完成 | 112 | lib_errors.h |
| ✅ 完成 | 432 | lib_vty.c |
| ✅ 完成 | 52 | lib_vty.h |
| ✅ 完成 | 115 | libagentx.c |
| ✅ 完成 | 51 | libagentx.h |
| ✅ 完成 | 1774 | libfrr.c |
| ✅ 完成 | 379 | libfrr.h |
| ✅ 完成 | 33 | libfrr_trace.c |
| ✅ 完成 | 274 | libfrr_trace.h |
| ✅ 完成 | 196 | libospf.h |
| ✅ 完成 | 3459 | link_state.c |
| ✅ 完成 | 1678 | link_state.h |
| ✅ 完成 | 622 | linklist.c |
| ✅ 完成 | 560 | linklist.h |
| ✅ 完成 | 831 | log.c |
| ✅ 完成 | 316 | log.h |
| ✅ 完成 | 705 | log_cli.c |
| ✅ 完成 | 220 | log_filter.c |
| ✅ 完成 | 925 | log_nb.c |
| ✅ 完成 | 519 | log_vty.c |
| ✅ 完成 | 103 | log_vty.h |
| ✅ 完成 | 509 | md5.c |
| ✅ 完成 | 136 | md5.h |
| ✅ 完成 | 321 | memory.c |
| ✅ 完成 | 238 | memory.h |
| ✅ 完成 | 1685 | mgmt_be_client.c |
| ✅ 完成 | 240 | mgmt_be_client.h |
| ✅ 完成 | 92 | mgmt_defines.h |
| ✅ 完成 | 1134 | mgmt_fe_client.c |
| ✅ 完成 | 500 | mgmt_fe_client.h |
| ✅ 完成 | 1178 | mgmt_msg.c |
| ✅ 完成 | 402 | mgmt_msg.h |
| ✅ 完成 | 245 | mgmt_msg_native.c |
| ✅ 完成 | 1297 | mgmt_msg_native.h |
| ✅ 完成 | 49 | mgmt_pb.h |
| ✅ 完成 | 223 | mlag.c |
| ✅ 完成 | 194 | mlag.h |
| ✅ 完成 | 305 | module.c |
| ✅ 完成 | 187 | module.h |
| ✅ 完成 | 268 | monotime.h |
| ✅ 完成 | 141 | mpls.c |
| ✅ 完成 | 308 | mpls.h |
| ✅ 完成 | 401 | netlink_parser.c |
| ✅ 完成 | 258 | netlink_parser.h |
| ✅ 完成 | 743 | netns_linux.c |
| ✅ 完成 | 200 | netns_other.c |
| ✅ 完成 | 207 | network.c |
| ✅ 完成 | 177 | network.h |
| ✅ 完成 | 1749 | nexthop.c |
| ✅ 完成 | 460 | nexthop.h |
| ✅ 完成 | 1616 | nexthop_group.c |
| ✅ 完成 | 267 | nexthop_group.h |
| ✅ 完成 | 59 | nexthop_group_private.h |
| ✅ 完成 | 3459 | northbound.c |
| ✅ 完成 | 2708 | northbound.h |
| ✅ 完成 | 2339 | northbound_cli.c |
| ✅ 完成 | 274 | northbound_cli.h |
| ✅ 完成 | 328 | northbound_db.c |
| ✅ 完成 | 140 | northbound_db.h |
| ✅ 完成 | 1001 | northbound_notif.c |
| ✅ 完成 | 2884 | northbound_oper.c |
| ✅ 完成 | 778 | northbound_sysrepo.c |
| ✅ 完成 | 259 | ns.h |
| ✅ 完成 | 248 | ntop.c |
| ✅ 完成 | 894 | openbsd-queue.h |
| ✅ 完成 | 794 | openbsd-tree.c |
| ✅ 完成 | 721 | openbsd-tree.h |
| ✅ 完成 | 223 | pbr.h |
| ✅ 完成 | 111 | pid_output.c |
| ✅ 完成 | 2045 | plist.c |
| ✅ 完成 | 174 | plist.h |
| ✅ 完成 | 140 | plist_int.h |
| ✅ 完成 | 2112 | prefix.c |
| ✅ 完成 | 1012 | prefix.h |
| ✅ 完成 | 444 | printfrr.h |
| ✅ 完成 | 917 | privs.c |
| ✅ 完成 | 245 | privs.h |
| ✅ 完成 | 596 | ptm_lib.c |
| ✅ 完成 | 151 | ptm_lib.h |
| ✅ 完成 | 410 | pullwr.c |
| ✅ 完成 | 206 | pullwr.h |
| ✅ 完成 | 75 | pw.h |
| ✅ 完成 | 210 | qobj.c |
| ✅ 完成 | 293 | qobj.h |
| ✅ 完成 | 217 | queue.h |
| ✅ 完成 | 619 | resolver.c |
| ✅ 完成 | 92 | resolver.h |
| ✅ 完成 | 188 | ringbuf.c |
| ✅ 完成 | 212 | ringbuf.h |
| ✅ 完成 | 77 | route_opaque.h |
| ✅ 完成 | 4216 | routemap.c |
| ✅ 完成 | 1557 | routemap.h |
| ✅ 完成 | 2077 | routemap_cli.c |
| ✅ 完成 | 1868 | routemap_northbound.c |
| ✅ 完成 | 66 | routing_nb.c |
| ✅ 完成 | 86 | routing_nb.h |
| ✅ 完成 | 190 | routing_nb_config.c |
| ✅ 完成 | 166 | sbuf.c |
| ✅ 完成 | 114 | sbuf.h |
| ✅ 完成 | 57 | segment_routing.c |
| ✅ 完成 | 63 | segment_routing.h |
| ✅ 完成 | 487 | seqlock.c |
| ✅ 完成 | 262 | seqlock.h |
| ✅ 完成 | 496 | sha256.c |
| ✅ 完成 | 104 | sha256.h |
| ✅ 完成 | 488 | sigevent.c |
| ✅ 完成 | 147 | sigevent.h |
| ✅ 完成 | 791 | skiplist.c |
| ✅ 完成 | 235 | skiplist.h |
| ✅ 完成 | 326 | smux.h |
| ✅ 完成 | 300 | snmp.c |
| ✅ 完成 | 1019 | sockopt.c |
| ✅ 完成 | 351 | sockopt.h |
| ✅ 完成 | 1062 | sockunion.c |
| ✅ 完成 | 253 | sockunion.h |
| ✅ 完成 | 432 | spf_backoff.c |
| ✅ 完成 | 121 | spf_backoff.h |
| ✅ 完成 | 453 | srcdest_table.c |
| ✅ 完成 | 197 | srcdest_table.h |
| ✅ 完成 | 75 | srte.h |
| ✅ 完成 | 710 | srv6.c |
| ✅ 完成 | 789 | srv6.h |
| ✅ 完成 | 1824 | stream.c |
| ✅ 完成 | 903 | stream.h |
| ✅ 完成 | 710 | strformat.c |
| ✅ 完成 | 98 | strlcat.c |
| ✅ 完成 | 81 | strlcpy.c |
| ✅ 完成 | 252 | systemd.c |
| ✅ 完成 | 79 | systemd.h |
| ✅ 完成 | 929 | table.c |
| ✅ 完成 | 533 | table.h |
| ✅ 完成 | 124 | tc.c |
| ✅ 完成 | 253 | tc.h |
| ✅ 完成 | 687 | termtable.c |
| ✅ 完成 | 434 | termtable.h |
| ✅ 完成 | 94 | trace.h |
| ✅ 完成 | 584 | typerb.c |
| ✅ 完成 | 326 | typerb.h |
| ✅ 完成 | 743 | typesafe.c |
| ✅ 完成 | 1473 | typesafe.h |
| ✅ 完成 | 321 | vector.c |
| ✅ 完成 | 140 | vector.h |
| ✅ 完成 | 38 | vlan.h |
| ✅ 完成 | 1260 | vrf.c |
| ✅ 完成 | 426 | vrf.h |
| ✅ 完成 | 69 | vrf_int.h |
| ✅ 完成 | 3943 | vty.c |
| ✅ 完成 | 610 | vty.h |
| ✅ 完成 | 83 | vxlan.h |
| ✅ 完成 | 218 | wheel.c |
| ✅ 完成 | 178 | wheel.h |
| ✅ 完成 | 554 | workqueue.c |
| ✅ 完成 | 293 | workqueue.h |
| ✅ 完成 | 188 | xref.c |
| ✅ 完成 | 419 | xref.h |
| ✅ 完成 | 2400 | yang.c |
| ✅ 完成 | 1305 | yang.h |
| ✅ 完成 | 668 | yang_translator.c |
| ✅ 完成 | 198 | yang_translator.h |
| ✅ 完成 | 1420 | yang_wrappers.c |
| ✅ 完成 | 335 | yang_wrappers.h |
| ✅ 完成 | 6312 | zclient.c |
| ✅ 完成 | 2012 | zclient.h |
| ✅ 完成 | 281 | zebra.h |
| ✅ 完成 | 1426 | zlog.c |
| ✅ 完成 | 478 | zlog.h |
| ✅ 完成 | 1321 | zlog_5424.c |
| ✅ 完成 | 222 | zlog_5424.h |
| ✅ 完成 | 1157 | zlog_5424_cli.c |
| ✅ 完成 | 347 | zlog_live.c |
| ✅ 完成 | 114 | zlog_live.h |
| ✅ 完成 | 109 | zlog_recirculate.c |
| ✅ 完成 | 26 | zlog_recirculate.h |
| ✅ 完成 | 784 | zlog_targets.c |
| ✅ 完成 | 110 | zlog_targets.h |
| | | **printf/ 与 assert/** |
| ✅ 完成 | 149 | assert/assert.h |
| ✅ 完成 | 385 | printf/glue.c |
| ✅ 完成 | 876 | printf/printf-pos.c |
| ✅ 完成 | 309 | printf/printfcommon.h |
| ✅ 完成 | 156 | printf/printflocal.h |
| ✅ 完成 | 980 | printf/vfprintf.c |
| | | **lex/yacc/cpp** |
| ✅ 完成 | 691 | command_parse.y |
| ✅ 完成 | 165 | command_lex.l |
| ✅ 完成 | 436 | defun_lex.l |
| ✅ 完成 | 1476 | northbound_grpc.cpp |
