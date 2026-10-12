/**
 @file ctc_greatbelt_l2.c

 @date 2009-11-2

 @version v2.0


*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"

#include "ctc_greatbelt_l2.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_fdb_sort.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
/****************************************************************************
 *
* Function
*
*****************************************************************************/

/**
 @brief init fdb module

 @param[in]  l2_fdb_global_cfg   init fdb module

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_fdb_init(uint8 lchip, void* l2_fdb_global_cfg)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    ctc_l2_fdb_global_cfg_t global_cfg;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    if (NULL == l2_fdb_global_cfg)
    {
        sal_memset(&global_cfg, 0, sizeof(ctc_l2_fdb_global_cfg_t));
      /* delete all entries if flush_fdb_cnt_per_loop=0, or delete  flush_fdb_cnt_per_loop entries one time*/
        global_cfg.flush_fdb_cnt_per_loop = 0;
        global_cfg.default_entry_rsv_num = 5 * 1024; /*5k*/
        global_cfg.hw_learn_en = 1;
        global_cfg.logic_port_num = 1 * 1024;
        global_cfg.trie_sort_en   = 0;
        l2_fdb_global_cfg = &global_cfg;
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_fdb_init(lchip, l2_fdb_global_cfg));
        if (TRUE == sys_greatbelt_l2_fdb_trie_sort_en(lchip))
        {
            CTC_ERROR_RETURN(sys_greatbelt_fdb_sort_init(lchip));
        }
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_l2_fdb_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end   = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        if (TRUE == sys_greatbelt_l2_fdb_trie_sort_en(lchip))
        {
            CTC_ERROR_RETURN(sys_greatbelt_fdb_sort_deinit(lchip));
        }
        CTC_ERROR_RETURN(sys_greatbelt_l2_fdb_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief    add a fdb entry

 @param[in] l2_addr        sys layer maintain the data from the ctc layer

 @return CTC_E_XXX

 @remarks To Add l2 fdb entry.
                for STATIC fdb,set CTC_L2_FLAG_IS_STATIC.
                for sys fdb,set CTC_L2_FLAG_SYSTEM_RSV,cannot be flush.
                for src discard fdb,set CTC_L2_FLAG_SRC_DISCARD.
                for STATIC fdb,set CTC_L2_FLAG_IS_STATIC can be flush.
                for send 2 cpu,set CTC_L2_FLAG_RAW_PKT_ELOG_CPU.
*/
int32
ctc_greatbelt_l2_add_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    uint8 lchip_start               = 0;
    uint8 lchip_end                 = 0;
    int32 ret                       = CTC_E_NONE;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_l2_add_fdb(lchip, l2_addr, 0, 0));
    }
    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_l2_remove_fdb(lchip, l2_addr);
    }

    return ret;
}

/**
 @brief Delete a fdb entry

 @param[in] l2_addr         sys layer maintain the data from the ctc layer

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_remove_fdb(uint8 lchip, ctc_l2_addr_t* l2_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_l2_remove_fdb(lchip, l2_addr));
    }

    return ret;
}

/**
 @brief  This function is to delete fdb entry by index

 @param[in] index  fdb node index

 @param[out] l2_addr      device-independent L2 unicast address

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_get_fdb_by_index(uint8 lchip, uint32 index, ctc_l2_addr_t* l2_addr)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_ERROR_RETURN(sys_greatbelt_l2_get_fdb_by_index(lchip, index, 1, l2_addr, NULL));
    return CTC_E_NONE;
}

/**
 @brief Delete a fdb entry by index

 @param[in] index    fdb node index

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_remove_fdb_by_index(uint8 lchip, uint32 index)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_l2_remove_fdb_by_index(lchip, index));
    }

    return ret;
}

/**
 @brief Add a fdb entry with nhp

 @param[in] l2_addr         sys layer maintain the data from the ctc layer

 @param[in] nhp_id           the specified nhp_id

 @return CTC_E_XXX

 @remarks To Add l2 fdb entry.
                for STATIC fdb,set CTC_L2_FLAG_IS_STATIC.
                for sys fdb,set CTC_L2_FLAG_SYSTEM_RSV,cannot be flush.
                for src discard fdb,set CTC_L2_FLAG_SRC_DISCARD.
                for STATIC fdb,set CTC_L2_FLAG_IS_STATIC can be flush.
                for send 2 cpu,set CTC_L2_FLAG_RAW_PKT_ELOG_CPU.

*/
int32
ctc_greatbelt_l2_add_fdb_with_nexthop(uint8 lchip, ctc_l2_addr_t* l2_addr, uint32 nhp_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_l2_add_fdb(lchip, l2_addr, 1, nhp_id));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_l2_remove_fdb(lchip, l2_addr);
    }

    return ret;
}

/**
 @brief Query fdb count according to specified query condition

 @param[in] pQuery                query condition

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_get_fdb_count(uint8 lchip, ctc_l2_fdb_query_t* pQuery)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_PTR_VALID_CHECK(pQuery);

    CTC_ERROR_RETURN(sys_greatbelt_l2_get_fdb_count(lchip, pQuery));
    return CTC_E_NONE;
}

/**
 @brief Query fdb enery according to specified query condition

 @param[in] pQuery                 query condition

 @param[in/out] query_rst      query results

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_get_fdb_entry(uint8 lchip, ctc_l2_fdb_query_t* pQuery, ctc_l2_fdb_query_rst_t* query_rst)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_PTR_VALID_CHECK(pQuery);
    CTC_PTR_VALID_CHECK(query_rst);

    if (TRUE == sys_greatbelt_l2_fdb_trie_sort_en(lchip))
    {
        /*in this mode, first query_rst->start_index must be 0, and quit after query_rst->is_end = 1*/
        CTC_ERROR_RETURN(sys_greatbelt_fdb_sort_get_fdb_entry(lchip, pQuery, query_rst));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_get_fdb_entry(lchip, pQuery, query_rst));
    }
    return CTC_E_NONE;
}

/**
 @brief Flush fdb entry by specified flush type

 @param[in] pFlush  flush type

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_flush_fdb(uint8 lchip, ctc_l2_flush_fdb_t* pFlush)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_PTR_VALID_CHECK(pFlush);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_flush_fdb(lchip, pFlush));
    }
    return CTC_E_NONE;
}

/**
 @brief Add an entry in the multicast table

 @param[in] l2mc_addr     L2 multicast address

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2mcast_add_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
   uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_l2mcast_add_addr(lchip, l2mc_addr));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_l2mcast_remove_addr(lchip, l2mc_addr);
    }

    return ret;
}

/**
 @brief Remove an entry in the multicast table

 @param[in] l2mc_addr     L2 multicast address

 @return CTC_E_XXX

*/
extern  int32
ctc_greatbelt_l2mcast_remove_addr(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_l2mcast_remove_addr(lchip, l2mc_addr));
    }

    return ret;
}

/**
 @brief Add a given port/port list to  a existed multicast group

 @param[in] l2mc_addr     L2 multicast address

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2mcast_add_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 idx                      = 0;
    uint8 pid                      = 0;
    uint8 lport                    = 0;
    uint8 lchip_temp               = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    if (l2mc_addr->port_bmp_en)
    {
        if (l2mc_addr->remote_chip)
        {
            return CTC_E_INVALID_PARAM;
        }
        if (l2mc_addr->gchip_id != CTC_LINKAGG_CHIPID)
        {
            SYS_MAP_GCHIP_TO_LCHIP(l2mc_addr->gchip_id, lchip_temp);
            SYS_LCHIP_CHECK_ACTIVE(lchip_temp);
        }
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            for (idx = 0; idx < sizeof(l2mc_addr->member.port_bmp) / 4; idx++)
            {
                if (l2mc_addr->member.port_bmp[idx] == 0)
                {
                    continue;
                }
                for (pid = 0; pid < 32; pid++)
                {
                    if (CTC_IS_BIT_SET(l2mc_addr->member.port_bmp[idx], pid))
                    {
                        lport = pid + idx * 32;
                        l2mc_addr->member.mem_port = CTC_MAP_LPORT_TO_GPORT(l2mc_addr->gchip_id, lport);

                        ret = sys_greatbelt_l2mcast_add_member(lchip, l2mc_addr);
                        if (ret < 0)
                        {
                            goto error;
                        }
                    }
                }
            }
        }
    }
    else
    {
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                     ret,
                                     sys_greatbelt_l2mcast_add_member(lchip, l2mc_addr));
        }

        /*rollback if error exist*/
        CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
        {
            sys_greatbelt_l2mcast_remove_member(lchip, l2mc_addr);
        }
    }
    return ret;

    error:
        ctc_greatbelt_l2mcast_remove_member(lchip, l2mc_addr);
        return ret;
}

/**
 @brief Remove a given port/port list to  a existed multicast group

 @param[in] l2mc_addr     L2 multicast address

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2mcast_remove_member(uint8 lchip, ctc_l2_mcast_addr_t* l2mc_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 idx                      = 0;
    uint8 pid                      = 0;
    uint8 lport                    = 0;
    uint8 lchip_temp               = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    if (l2mc_addr->port_bmp_en)
    {
        if (l2mc_addr->remote_chip)
        {
            return CTC_E_INVALID_PARAM;
        }
        if (l2mc_addr->gchip_id != CTC_LINKAGG_CHIPID)
        {
            SYS_MAP_GCHIP_TO_LCHIP(l2mc_addr->gchip_id, lchip_temp);
            SYS_LCHIP_CHECK_ACTIVE(lchip_temp);
        }
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            for (idx = 0; idx < sizeof(l2mc_addr->member.port_bmp) / 4; idx++)
            {
                if (l2mc_addr->member.port_bmp[idx] == 0)
                {
                    continue;
                }
                for (pid = 0; pid < 32; pid++)
                {
                    if (CTC_IS_BIT_SET(l2mc_addr->member.port_bmp[idx], pid))
                    {
                        lport = pid + idx * 32;
                        l2mc_addr->member.mem_port = CTC_MAP_LPORT_TO_GPORT(l2mc_addr->gchip_id, lport);
                        sys_greatbelt_l2mcast_remove_member(lchip, l2mc_addr);
                    }
                }
            }
        }
    }
    else
    {
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                     ret,
                                     sys_greatbelt_l2mcast_remove_member(lchip, l2mc_addr));
        }
    }
    return ret;

}


/**
 @brief add a default entry

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX

*/
int32
ctc_greatbelt_l2_add_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

	LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_l2_add_default_entry(lchip, l2dflt_addr));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_greatbelt_l2_remove_default_entry(lchip, l2dflt_addr);
    }

    return ret;

}

/**
 @brief remove default entry

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_remove_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;

	LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);


    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                 ret,
                                 sys_greatbelt_l2_remove_default_entry(lchip, l2dflt_addr));
    }

    return ret;
}

/**
 @brief add a port into default entry

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_add_port_to_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 idx                      = 0;
    uint8 pid                      = 0;
    uint8 lport                    = 0;
    uint8 lchip_temp               = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    if (l2dflt_addr->port_bmp_en)
    {
        if (l2dflt_addr->remote_chip)
        {
            return CTC_E_INVALID_PARAM;
        }
        if (l2dflt_addr->gchip_id != CTC_LINKAGG_CHIPID)
        {
            SYS_MAP_GCHIP_TO_LCHIP(l2dflt_addr->gchip_id, lchip_temp);
            SYS_LCHIP_CHECK_ACTIVE(lchip_temp);
        }
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            for (idx = 0; idx < sizeof(l2dflt_addr->member.port_bmp) / 4; idx++)
            {
                if (l2dflt_addr->member.port_bmp[idx] == 0)
                {
                    continue;
                }
                for (pid = 0; pid < 32; pid++)
                {
                    if (CTC_IS_BIT_SET(l2dflt_addr->member.port_bmp[idx], pid))
                    {
                        lport = pid + idx * 32;
                        l2dflt_addr->member.mem_port = CTC_MAP_LPORT_TO_GPORT(l2dflt_addr->gchip_id, lport);

                         ret = sys_greatbelt_l2_add_port_to_default_entry(lchip, l2dflt_addr);
                         if (ret < 0)
                         {
                            goto error;
                         }
                    }
                }
            }
        }
    }
    else
    {
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                     ret,
                                     sys_greatbelt_l2_add_port_to_default_entry(lchip, l2dflt_addr));
        }

        /*rollback if error exist*/
        CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
        {
            sys_greatbelt_l2_remove_port_from_default_entry(lchip, l2dflt_addr);
        }
    }

    return ret;

    error:
        ctc_greatbelt_l2_remove_port_from_default_entry(lchip, l2dflt_addr);
        return ret;

}

/**
 @brief remove a port from default entry

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_remove_port_from_default_entry(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    int32 ret                      = CTC_E_NONE;
    uint8 idx                      = 0;
    uint8 pid                      = 0;
    uint8 lport                    = 0;
    uint8 lchip_temp               = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    if (l2dflt_addr->port_bmp_en)
    {
        if (l2dflt_addr->remote_chip)
        {
            return CTC_E_INVALID_PARAM;
        }
        if (l2dflt_addr->gchip_id != CTC_LINKAGG_CHIPID)
        {
            SYS_MAP_GCHIP_TO_LCHIP(l2dflt_addr->gchip_id, lchip_temp);
            SYS_LCHIP_CHECK_ACTIVE(lchip_temp);
        }
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            for (idx = 0; idx < sizeof(l2dflt_addr->member.port_bmp) / 4; idx++)
            {
                if (l2dflt_addr->member.port_bmp[idx] == 0)
                {
                    continue;
                }
                for (pid = 0; pid < 32; pid++)
                {
                    if (CTC_IS_BIT_SET(l2dflt_addr->member.port_bmp[idx], pid))
                    {
                        lport = pid + idx * 32;
                        l2dflt_addr->member.mem_port = CTC_MAP_LPORT_TO_GPORT(l2dflt_addr->gchip_id, lport);
                        sys_greatbelt_l2_remove_port_from_default_entry(lchip, l2dflt_addr);
                    }
                }
            }
        }
    }
    else
    {
        CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
        {
            CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_NOT_EXIST,
                                     ret,
                                     sys_greatbelt_l2_remove_port_from_default_entry(lchip, l2dflt_addr));
        }
    }
    return ret;
}

/**
 @brief add a port into default entry

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_set_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_L2);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_set_default_entry_features(lchip, l2dflt_addr));
    }

    return CTC_E_NONE;
}

/**
 @brief add a port into default entry

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_get_default_entry_features(uint8 lchip, ctc_l2dflt_addr_t* l2dflt_addr)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_ERROR_RETURN(sys_greatbelt_l2_get_default_entry_features(lchip, l2dflt_addr));
    return CTC_E_NONE;
}

/**
 @brief set nhid logic_port mapping table

 @param[in]   logic_port   nhp_id
 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_set_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32 nhp_id)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
         CTC_ERROR_RETURN(sys_greatbelt_l2_set_nhid_by_logic_port(lchip, logic_port, nhp_id));
    }
    return CTC_E_NONE;
}

/**
 @brief set nhid by logic_port from mapping table

 @param[in] l2dflt_addr   point to ctc_l2dflt_addr_t

 @return CTC_E_XXX
*/
int32
ctc_greatbelt_l2_get_nhid_by_logic_port(uint8 lchip, uint16 logic_port, uint32* nhp_id)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    CTC_ERROR_RETURN(sys_greatbelt_l2_get_nhid_by_logic_port(lchip, logic_port, nhp_id));
    return CTC_E_NONE;
}

int32
ctc_greatbelt_l2_fdb_set_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8 hit)
{
    uint8 lchip_start              = 0;
    uint8 lchip_end                = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_l2_fdb_set_entry_hit(lchip, l2_addr, hit));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_l2_fdb_get_entry_hit(uint8 lchip, ctc_l2_addr_t* l2_addr, uint8* hit)
{
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_l2_fdb_get_entry_hit(lchip, l2_addr, hit));

    return CTC_E_NONE;
}

int32
ctc_greatbelt_l2_replace_fdb(uint8 lchip, ctc_l2_replace_t* p_replace)
{
    uint8 lchip_start               = 0;
    uint8 lchip_end                 = 0;
    int32 ret                       = CTC_E_NONE;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_L2);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_ENTRY_EXIST,
                                 ret,
                                 sys_greatbelt_l2_replace_fdb(lchip, p_replace));
    }

    return ret;
}

