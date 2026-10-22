# bgpd 源码注释进度表

分支: study/bgpd-annotated | 注释语言: 中文 | 粒度: 函数级 + 关键行/数据结构
规则: 只增注释, 不改任何代码 (每文件用 git diff 校验: 删除行必须为 0)

## 校验方式（三层，每完成一个文件都要跑）
1. 新增/删除行: `git diff --numstat -- <file>` 第二列必须为 0
2. 新增行必须全是注释:
   `git diff -U0 -- <file> | grep -E '^\+' | grep -v '^+++' | grep -vE '^\+[[:space:]]*(/\*|\*|//)'`
   必须为空(允许极少数空行)
3. **最强校验(推荐每次都跑)**: 剥离注释后比较代码流
   `perl "C:/Users/ASUS/AppData/Local/Temp/bgpd_annot_check.pl" D:/share/runos/frr-frr-10.6.1 <git 相对路径>`
   它把 HEAD 版与工作版都做“去掉注释后的代码行比较”，输出 `OK` 才算通过。
   它能发现前两层发现不了的两类错误:
   - 注释正文里写了 `*/`，导致注释块**提前闭合**、后面的中文变成裸代码(直接编译不过)
   - 插入注释时忘了把紧随其后的代码行带回来 / 重复了一遍，导致**代码行重复**
   (辅助诊断脚本: `C:/Users/ASUS/AppData/Local/Temp/bgpd_leakscan.pl`)
4. 完整性核对: `bgp_cjk_scan.pl`（统计每个文件“最后一个含中文注释的行”的位置），
   如果它远远没到文件末尾，说明该文件只注释了前半段，需要续做。
   注意：头文件尾部常是 `#endif` / 收尾大括号，位置在 85%~90% 就已算完整。

## 注释写作禁忌
- 中文注释正文中**禁止出现 `*/` 或 `/*`**。要表达“A 和 B 两类”请写成 `A / B`(斜杠两侧留空格)。
  踩雷例子: `bgp_attr_get_*/set_*`、`*_install/*_send`、`frr-*/ *.py`、`is_vni_*/bgpevpn_get_*`。
- 不要把中文块插在已有英文注释块**内部**(锚点若在注释块中间，块尾的 `*/` 会提前闭合它)。
  正确做法: 中文块单独起一个 `/* ... */`，英文原注释块保持一字不动(包括它自己的 `/*` 起始行)。

## 说明
- “行数”列是**当前工作区**的行数(即注释后的行数)，不是原始行数。
- 状态: ✅ 完成 = 已注释且通过三层校验; 🔄 进行中 = 已派发但尚未交付/尚未校验。

## 完成情况 (2026-10-13 收尾)
131 个 .c/.h 文件**全部完成**，最终全仓校验结果:
- 代码流比对(剥离注释后逐行比较): **131 个文件全部 OK，0 BAD** —— 没有任何代码行被改动、丢失或重复。
- 删除行: 默认 diff 与 `--minimal` diff 下**全部为 0**。
- 新增行全部是注释: 通过(过滤后无残留的非注释行)。
- 中文泄漏检查: 无任何中文出现在注释之外。

规模: 原始代码约 176,794 行 → 注释后 201,610 行，**新增注释约 24,816 行，删除 0 行**。

注: 个别文件(如 bgp_evpn.c、bgp_encap_tlv.h)在插入注释后，git 默认的 Myers 差分曾出现“伪删除”
(被删的那一行与 HEAD 逐字节相同，属差分算法非最小化所致；`--minimal` 下显示 0 删除)。
遇到删除数不为 0 时，请先跑 `git diff --numstat --minimal -- <file>` 与上面的比较器确认，
不要盲目去改代码。

| 状态 | 行数 | 文件 |
|------|------|------|
| ✅ 完成 | 615 | bgp_addpath.c |
| ✅ 完成 | 147 | bgp_addpath.h |
| ✅ 完成 | 106 | bgp_addpath_types.h |
| ✅ 完成 | 369 | bgp_advertise.c |
| ✅ 完成 | 294 | bgp_advertise.h |
| ✅ 完成 | 3094 | bgp_aspath.c |
| ✅ 完成 | 400 | bgp_aspath.h |
| ✅ 完成 | 6525 | bgp_attr.c |
| ✅ 完成 | 851 | bgp_attr.h |
| ✅ 完成 | 392 | bgp_attr_evpn.c |
| ✅ 完成 | 119 | bgp_attr_evpn.h |
| ✅ 完成 | 75 | bgp_attr_srv6.h |
| ✅ 完成 | 982 | bgp_bfd.c |
| ✅ 完成 | 122 | bgp_bfd.h |
| ✅ 完成 | 4046 | bgp_bmp.c |
| ✅ 完成 | 449 | bgp_bmp.h |
| ✅ 完成 | 323 | bgp_btoa.c |
| ✅ 完成 | 1742 | bgp_clist.c |
| ✅ 完成 | 322 | bgp_clist.h |
| ✅ 完成 | 1192 | bgp_community.c |
| ✅ 完成 | 187 | bgp_community.h |
| ✅ 完成 | 291 | bgp_community_alias.c |
| ✅ 完成 | 106 | bgp_community_alias.h |
| ✅ 完成 | 596 | bgp_conditional_adv.c |
| ✅ 完成 | 108 | bgp_conditional_adv.h |
| ✅ 完成 | 1091 | bgp_damp.c |
| ✅ 完成 | 235 | bgp_damp.h |
| ✅ 完成 | 3384 | bgp_debug.c |
| ✅ 完成 | 305 | bgp_debug.h |
| ✅ 完成 | 1079 | bgp_dump.c |
| ✅ 完成 | 102 | bgp_dump.h |
| ✅ 完成 | 2345 | bgp_ecommunity.c |
| ✅ 完成 | 590 | bgp_ecommunity.h |
| ✅ 完成 | 1163 | bgp_encap_tlv.c |
| ✅ 完成 | 192 | bgp_encap_tlv.h |
| ✅ 完成 | 385 | bgp_encap_types.h |
| ✅ 完成 | 624 | bgp_errors.c |
| ✅ 完成 | 147 | bgp_errors.h |
| ✅ 完成 | 10546 | bgp_evpn.c |
| ✅ 完成 | 417 | bgp_evpn.h |
| ✅ 完成 | 5934 | bgp_evpn_mh.c |
| ✅ 完成 | 755 | bgp_evpn_mh.h |
| ✅ 完成 | 1171 | bgp_evpn_private.h |
| ✅ 完成 | 8584 | bgp_evpn_vty.c |
| ✅ 完成 | 91 | bgp_evpn_vty.h |
| ✅ 完成 | 950 | bgp_filter.c |
| ✅ 完成 | 127 | bgp_filter.h |
| ✅ 完成 | 256 | bgp_flowspec.c |
| ✅ 完成 | 121 | bgp_flowspec.h |
| ✅ 完成 | 69 | bgp_flowspec_private.h |
| ✅ 完成 | 868 | bgp_flowspec_util.c |
| ✅ 完成 | 121 | bgp_flowspec_util.h |
| ✅ 完成 | 744 | bgp_flowspec_vty.c |
| ✅ 完成 | 4061 | bgp_fsm.c |
| ✅ 完成 | 296 | bgp_fsm.h |
| ✅ 完成 | 880 | bgp_io.c |
| ✅ 完成 | 147 | bgp_io.h |
| ✅ 完成 | 376 | bgp_keepalives.c |
| ✅ 完成 | 101 | bgp_keepalives.h |
| ✅ 完成 | 844 | bgp_label.c |
| ✅ 完成 | 253 | bgp_label.h |
| ✅ 完成 | 1994 | bgp_labelpool.c |
| ✅ 完成 | 186 | bgp_labelpool.h |
| ✅ 完成 | 761 | bgp_lcommunity.c |
| ✅ 完成 | 165 | bgp_lcommunity.h |
| ✅ 完成 | 542 | bgp_mac.c |
| ✅ 完成 | 67 | bgp_mac.h |
| ✅ 完成 | 639 | bgp_main.c |
| ✅ 完成 | 195 | bgp_memory.c |
| ✅ 完成 | 216 | bgp_memory.h |
| ✅ 完成 | 863 | bgp_mpath.c |
| ✅ 完成 | 172 | bgp_mpath.h |
| ✅ 完成 | 5103 | bgp_mplsvpn.c |
| ✅ 完成 | 609 | bgp_mplsvpn.h |
| ✅ 完成 | 1810 | bgp_mplsvpn_snmp.c |
| ✅ 完成 | 44 | bgp_mplsvpn_snmp.h |
| ✅ 完成 | 1360 | bgp_network.c |
| ✅ 完成 | 90 | bgp_network.h |
| ✅ 完成 | 1605 | bgp_nexthop.c |
| ✅ 完成 | 261 | bgp_nexthop.h |
| ✅ 完成 | 158 | bgp_nhc.c |
| ✅ 完成 | 131 | bgp_nhc.h |
| ✅ 完成 | 147 | bgp_nhg.c |
| ✅ 完成 | 45 | bgp_nhg.h |
| ✅ 完成 | 2086 | bgp_nht.c |
| ✅ 完成 | 172 | bgp_nht.h |
| ✅ 完成 | 2409 | bgp_open.c |
| ✅ 完成 | 221 | bgp_open.h |
| ✅ 完成 | 4569 | bgp_packet.c |
| ✅ 完成 | 171 | bgp_packet.h |
| ✅ 完成 | 3331 | bgp_pbr.c |
| ✅ 完成 | 504 | bgp_pbr.h |
| ✅ 完成 | 340 | bgp_rd.c |
| ✅ 完成 | 142 | bgp_rd.h |
| ✅ 完成 | 123 | bgp_regex.c |
| ✅ 完成 | 43 | bgp_regex.h |
| ✅ 完成 | 19317 | bgp_route.c |
| ✅ 完成 | 1198 | bgp_route.h |
| ✅ 完成 | 9682 | bgp_routemap.c |
| ✅ 完成 | 564 | bgp_routemap_nb.c |
| ✅ 完成 | 276 | bgp_routemap_nb.h |
| ✅ 完成 | 4154 | bgp_routemap_nb_config.c |
| ✅ 完成 | 3165 | bgp_rpki.c |
| ✅ 完成 | 51 | bgp_rpki.h |
| ✅ 完成 | 250 | bgp_script.c |
| ✅ 完成 | 82 | bgp_script.h |
| ✅ 完成 | 222 | bgp_snmp.c |
| ✅ 完成 | 57 | bgp_snmp.h |
| ✅ 完成 | 908 | bgp_snmp_bgp4.c |
| ✅ 完成 | 142 | bgp_snmp_bgp4.h |
| ✅ 完成 | 1731 | bgp_snmp_bgp4v2.c |
| ✅ 完成 | 141 | bgp_snmp_bgp4v2.h |
| ✅ 完成 | 424 | bgp_srv6.c |
| ✅ 完成 | 84 | bgp_srv6.h |
| ✅ 完成 | 346 | bgp_table.c |
| ✅ 完成 | 540 | bgp_table.h |
| ✅ 完成 | 28 | bgp_trace.c |
| ✅ 完成 | 1753 | bgp_trace.h |
| ✅ 完成 | 2791 | bgp_updgrp.c |
| ✅ 完成 | 738 | bgp_updgrp.h |
| ✅ 完成 | 1436 | bgp_updgrp_adv.c |
| ✅ 完成 | 1565 | bgp_updgrp_packet.c |
| ✅ 完成 | 40 | bgp_vnc_types.h |
| ✅ 完成 | 263 | bgp_vpn.c |
| ✅ 完成 | 38 | bgp_vpn.h |
| ✅ 完成 | 24654 | bgp_vty.c |
| ✅ 完成 | 319 | bgp_vty.h |
| ✅ 完成 | 5520 | bgp_zebra.c |
| ✅ 完成 | 229 | bgp_zebra.h |
| ✅ 完成 | 11268 | bgpd.c |
| ✅ 完成 | 3553 | bgpd.h |
