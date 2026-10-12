/**
 @file sys_greatbelt_queue_enq.c

 @date 2010-01-13

 @version v2.0

*/

/****************************************************************************
 *  Default Queue Mode
 *  current queue allocation scheme (Alt + F12 for pretty):
 *
 *           type          port id    channel id      queue number    queue id range
 *     ----------------  ----------   ----------      ------------    --------------
 *      network egress     0 - 51      0 - 51           8 * 52           0 - 415

 ****************************************************************************/

/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/

#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"

#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_opf.h"
#include "sys_greatbelt_internal_port.h"
#include "sys_greatbelt_stats.h"
#include "sys_greatbelt_nexthop_api.h"
#include "sys_greatbelt_nexthop.h"

#include "sys_greatbelt_queue_enq.h"
#include "sys_greatbelt_queue_sch.h"
#include "sys_greatbelt_cpu_reason.h"
#include "sys_greatbelt_queue_shape.h"
#include "sys_greatbelt_queue_drop.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_data_path.h"

/****************************************************************************
 *
 * Global and Declaration
 *
 ****************************************************************************/

#define SYS_QUEUE_VEC_BLOCK_SIZE 64
#define SYS_QUEUE_VEC_BLOCK_NUM (SYS_MAX_QUEUE_NUM / SYS_QUEUE_VEC_BLOCK_SIZE)

#define SYS_QUEUE_GRP_VEC_BLOCK_SIZE 64
#define SYS_QUEUE_GRP_VEC_BLOCK_NUM (SYS_MAX_QUEUE_GROUP_NUM / SYS_QUEUE_GRP_VEC_BLOCK_SIZE)

#define SYS_REASON_VEC_BLOCK_SIZE 8
#define SYS_REASON_VEC_BLOCK_NUM ((CTC_PKT_CPU_REASON_MAX_COUNT + (SYS_REASON_VEC_BLOCK_SIZE - 1)) / SYS_REASON_VEC_BLOCK_SIZE)

#define SYS_QUEUE_HASH_MAX   256
#define SYS_QUEUE_HASH_BLOCK_SIZE 8
#define SYS_QUEUE_HASH_BLOCK_NUM (SYS_QUEUE_HASH_MAX + (SYS_QUEUE_HASH_BLOCK_SIZE - 1) / SYS_QUEUE_HASH_BLOCK_SIZE)

#define SYS_SERVICE_HASH_MAX   1024
#define SYS_SERVICE_HASH_BLOCK_SIZE 8
#define SYS_SERVICE_HASH_BLOCK_NUM (SYS_SERVICE_HASH_MAX + (SYS_SERVICE_HASH_BLOCK_SIZE - 1) / SYS_SERVICE_HASH_BLOCK_SIZE)


#define SYS_REASON_GRP_QUEUE_NUM 8

#define SYS_QUEUE_MAX_GRP_NUM_FOR_CPU_REASON 16
#define SYS_SHAPE_MAX_TOKEN_RATE          0x3FFFFF
#define SYS_SHAPE_MAX_TOKEN_THRD          0xFF
#define SYS_SHAPE_MAX_TOKEN_THRD_SHIFT    0xF

#define SYS_QNUM_GEN_CTL_INDEX(fabric, que_on_chip, src_qsel, dest_chip_match, qsel_type) \
    ((((fabric) & 0x1) << 7) | \
     (((que_on_chip) & 0x1) << 6) | \
     (((src_qsel) & 0x1) << 5) | \
     (((dest_chip_match) & 0x1) << 4) | \
     ((qsel_type) & 0xF))

#define SYS_RESRC_IGS_COND(tc_min,total,sc,tc,port,port_tc) \
    ((tc_min<<5)|(total<<4)|(sc<<3)|(tc<<2)|(port <<1)|(port_tc<<0))

sys_queue_master_t* p_gb_queue_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};


/****************************************************************************
 *
 * Function
 *
 ****************************************************************************/
uint32
sys_greatbelt_service_hash_make(sys_service_node_t* p_node)
{
    uint32 data[1] = {0};
    uint32 length = 0;

    data[0] = p_node->service_id;
    length = sizeof(uint32)*1;

    return ctc_hash_caculate(length, (uint8*)data);
}

bool
sys_greatbelt_service_hash_cmp(sys_service_node_t* p_node0,
                                  sys_service_node_t* p_node1)
{
    if (p_node0->service_id == p_node1->service_id)
    {
        return TRUE;
    }

    return FALSE;
}


sys_service_node_t*
sys_greatbelt_service_lookup(uint8 lchip, sys_service_node_t* p_node)
{
    return ctc_hash_lookup(p_gb_queue_master[lchip]->p_service_hash, p_node);
}

sys_service_node_t*
sys_greatbelt_service_add(uint8 lchip, sys_service_node_t* p_node)
{
    return ctc_hash_insert(p_gb_queue_master[lchip]->p_service_hash, p_node);
}

sys_service_node_t*
sys_greatbelt_service_remove(uint8 lchip, sys_service_node_t* p_node)
{
    return ctc_hash_remove(p_gb_queue_master[lchip]->p_service_hash, p_node);
}


uint32
sys_greatbelt_queue_info_hash_make(sys_queue_info_t* p_queue_info)
{
    uint32 data[2] = {0};
    uint32 length = 0;

    data[0] = p_queue_info->queue_type;
    data[1] = ((p_queue_info->service_id << 16) | p_queue_info->dest_queue);
    length = sizeof(uint32)*2;

    return ctc_hash_caculate(length, (uint8*)data);
}

bool
sys_greatbelt_queue_info_hash_cmp(sys_queue_info_t* p_data0,
                                  sys_queue_info_t* p_data1)
{
    if (p_data0->queue_type == p_data1->queue_type &&
        p_data0->dest_queue == p_data1->dest_queue &&
    p_data0->service_id == p_data1->service_id)
    {
        return TRUE;
    }

    return FALSE;
}



sys_queue_info_t*
sys_greatbelt_queue_info_lookup(uint8 lchip,
                             sys_queue_info_t* p_queue_info)
{
    return ctc_hash_lookup(p_gb_queue_master[lchip]->p_queue_hash, p_queue_info);
}

sys_queue_info_t*
sys_greatbelt_queue_info_add(uint8 lchip,
                             sys_queue_info_t* p_queue_info)
{
    return ctc_hash_insert(p_gb_queue_master[lchip]->p_queue_hash, p_queue_info);
}


sys_queue_info_t*
sys_greatbelt_queue_info_remove(uint8 lchip,
                             sys_queue_info_t* p_queue_info)
{
    return ctc_hash_remove(p_gb_queue_master[lchip]->p_queue_hash, p_queue_info);
}

int32
sys_greatbelt_queue_info_dump(void *bucket_data, void *data)
{
    sys_queue_info_t *p_queue_info =  NULL;
    p_queue_info = (sys_queue_info_t*)bucket_data;

    SYS_QUEUE_DBG_DUMP("service_id:%d, queue_type:%d, dest_queue:%d\n", p_queue_info->service_id,
                       p_queue_info->queue_type, p_queue_info->dest_queue);

    return CTC_E_NONE;

}
int32
sys_greatbelt_queue_info_traverse(uint8 lchip)
{
    SYS_QUEUE_INIT_CHECK(lchip);
    return ctc_hash_traverse(p_gb_queue_master[lchip]->p_queue_hash,
                             (hash_traversal_fn)sys_greatbelt_queue_info_dump,
                             NULL);
}


int32
sys_greatbelt_qos_create_service(uint8 lchip, uint32 service_id)
{
    sys_service_node_t* p_service = NULL;
    sys_service_node_t service;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&service, 0, sizeof(service));
    service.service_id = service_id;
    p_service = sys_greatbelt_service_lookup(lchip, &service);
    if (NULL != p_service)
    {
        return CTC_E_ENTRY_EXIST;
    }
    p_service =  mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_service_node_t));
    if (NULL == p_service)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_service, 0, sizeof(sys_service_node_t));
    p_service->service_id= service_id;
    p_service->p_logic_port_list = ctc_slist_new();
    p_service->p_dest_port_list = ctc_slist_new();
    sys_greatbelt_service_add(lchip, p_service);

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_destroy_service(uint8 lchip, uint32 service_id)
{
    sys_service_node_t* p_service = NULL;
    sys_service_node_t service;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&service, 0, sizeof(service));
    service.service_id = service_id;
    p_service = sys_greatbelt_service_lookup(lchip, &service);
    if (NULL == p_service)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }
    sys_greatbelt_service_remove(lchip, p_service);

    ctc_slist_free(p_service->p_logic_port_list);
    ctc_slist_free(p_service->p_dest_port_list);

    mem_free(p_service);

    return CTC_E_NONE;
}



/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_get_queue_id
 Description  : get queue id by queue type
 Input        : uint8 lchip
                uint8 queue_type
                uint8 queue_offset
                uint16 logic_id
 Output       :  uint16 *queue_id
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/21
 Modification : Created function

*****************************************************************************/
int32
_sys_greatbelt_queue_get_queue_id(uint8 lchip,
                                  ctc_qos_queue_id_t* p_queue,
                                  uint16* queue_id)
{
    uint16 queue_offset = 0;
    uint16 sub_queue_id = 0;

    SYS_QUEUE_DBG_FUNC();

    queue_offset = p_queue->queue_id;

    switch (p_queue->queue_type)
    {
    case CTC_QUEUE_TYPE_NETWORK_EGRESS:
        {
            uint16 gport = p_queue->gport;
            uint8 queue_num = 0;
            uint8 lport = 0;
            SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
            queue_num = p_gb_queue_master[lchip]->channel[lport].queue_info.queue_num;
            CTC_MAX_VALUE_CHECK(queue_offset, queue_num - 1);
            *queue_id = p_gb_queue_master[lchip]->channel[lport].queue_info.queue_base + queue_offset;
        }
        break;

    case CTC_QUEUE_TYPE_INTERNAL_PORT:
        {
            uint16 gport = p_queue->gport;
            uint8 queue_num = 0;
            uint8 internal_port = 0;
            SYS_MAP_GPORT_TO_LPORT(gport, lchip, internal_port);
            if (internal_port > SYS_RESERVE_PORT_MAX)
            {
                CTC_VALUE_RANGE_CHECK(internal_port, SYS_INTERNAL_PORT_START, SYS_INTERNAL_PORT_START + SYS_INTERNAL_PORT_NUM -1);
            }
            queue_num = p_gb_queue_master[lchip]->channel[internal_port].queue_info.queue_num;
            CTC_MAX_VALUE_CHECK(queue_offset, queue_num - 1);
            *queue_id = p_gb_queue_master[lchip]->channel[internal_port].queue_info.queue_base + queue_offset;
        }
        break;

    case CTC_QUEUE_TYPE_EXCP_CPU:
        {
            uint8 offset = 0;
            uint16 reason_id = p_queue->cpu_reason;
            sys_queue_reason_t* p_reason = NULL;

            SYS_QUEUE_DBG_INFO("reason_id = %d\n", reason_id);

            sys_greatbelt_cpu_reason_get_queue_offset(lchip, reason_id, &offset, &sub_queue_id);

            p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);
            if (NULL == p_reason)
            {
                return CTC_E_INVALID_PARAM;
            }
            if (reason_id == CTC_PKT_CPU_REASON_FWD_CPU)
            {
                CTC_MAX_VALUE_CHECK(queue_offset, p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_INTERNAL].queue_num - 1);
                *queue_id = p_reason->queue_info.queue_base + offset + queue_offset;
            }
            else if (reason_id == CTC_PKT_CPU_REASON_C2C_PKT)
            {
                CTC_MAX_VALUE_CHECK(queue_offset, 2*p_gb_queue_master[lchip]->cpu_reason_grp_que_num - 1);
                *queue_id = p_reason->queue_info.queue_base + offset + queue_offset;
            }
            else
            {
                CTC_MAX_VALUE_CHECK(queue_offset, 0);
                *queue_id = p_reason->queue_info.queue_base + offset;
            }

        }
        break;


    case CTC_QUEUE_TYPE_SERVICE_INGRESS:
        {
            uint8 lport = 0;
            sys_queue_info_t queue_info;
            sys_queue_info_t *p_queue_info = NULL;
            SYS_MAP_GPORT_TO_LPORT(p_queue->gport, lchip, lport);

            sal_memset(&queue_info, 0, sizeof(sys_queue_info_t));
            queue_info.service_id = p_queue->service_id;
            queue_info.dest_queue = lport;
            queue_info.queue_type = SYS_QUEUE_TYPE_SERVICE;
            p_queue_info = sys_greatbelt_queue_info_lookup(lchip, &queue_info);

            if (NULL == p_queue_info)
            {
                return CTC_E_INVALID_PARAM;
            }
            CTC_MAX_VALUE_CHECK(queue_offset, p_queue_info->queue_num - 1);
            *queue_id = p_queue_info->queue_base + queue_offset;
        }
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_get_qsel_shift
 Description  : get queue select shift as 2^n = queue_num
 Input        : uint8 queue_num
 Output       : uint8 *p_shift
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/21
 Modification : Created function

*****************************************************************************/
int32
_sys_greatbelt_queue_get_qsel_shift(uint8 lchip, uint8 queue_num,
                                    uint8* p_shift)
{
    uint8 i = 0;
    uint8 shift_num = 0;

    for (i = 0; i < 16; i++)
    {
        if ((1 << i) == queue_num)
        {
            shift_num = (p_gb_queue_master[lchip]->priority_mode)? 4:6;
            *p_shift = (shift_num-i);
            return CTC_E_NONE;
        }
    }

    return CTC_E_INVALID_PARAM;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_id_alloc
 Description  : queue id alloc from queue opf pool and add to db
 Input        : uint8 lchip
                uint8 queue_num
 Output       : uint16 *queue_base
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/21
 Modification : Created function

*****************************************************************************/
STATIC int32
_sys_greatbelt_queue_id_alloc(uint8 lchip,
                              uint8 queue_num,
                              uint16* queue_base)
{
    uint16 queue_id = 0;
    uint8  index    = 0;
    uint32 offset   = 0;
    sys_greatbelt_opf_t opf;
    sys_queue_node_t* p_sys_queue_node = NULL;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE;
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, queue_num, &offset));
    *queue_base = offset;

    for (index = 0; index < queue_num; index++)
    {
        queue_id = *queue_base + index;
        /*Don't consider memeroy roolback*/
        p_sys_queue_node = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_node_t));
        if (NULL == p_sys_queue_node)
        {
            sys_greatbelt_opf_free_offset(lchip, &opf, queue_num, offset);
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_sys_queue_node, 0, sizeof(sys_queue_node_t));
        p_sys_queue_node->queue_id = queue_id;
        p_sys_queue_node->channel = SYS_MAX_CHANNEL_NUM;
        p_sys_queue_node->group_id = SYS_MAX_QUEUE_GROUP_NUM;
        p_sys_queue_node->confirm_class = index;
        p_sys_queue_node->exceed_class = index;
        p_sys_queue_node->confirm_weight = 1;
        p_sys_queue_node->exceed_weight = 1;
        p_sys_queue_node->offset = index;
        ctc_vector_add(p_gb_queue_master[lchip]->queue_vec, queue_id, p_sys_queue_node);
    }

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_id_free
 Description  : queue id free form opf pool
 Input        : uint8 lchip
                uint8 queue_num
                uint16 queue_base
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/22
 Modification : Created function

*****************************************************************************/
STATIC int32
_sys_greatbelt_queue_id_free(uint8 lchip,
                             uint8 queue_num,
                             uint16 queue_base)
{
    uint16 queue_id = 0;
    uint8  index    = 0;
    uint32 offset   = queue_base;
    sys_greatbelt_opf_t opf;
    sys_queue_node_t* p_sys_queue_node = NULL;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, queue_num, offset));

    for (index = 0; index < queue_num; index++)
    {
        queue_id = offset + index;
        p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

        if (NULL != p_sys_queue_node)
        {
            ctc_vector_del(p_gb_queue_master[lchip]->queue_vec, queue_id);
            mem_free(p_sys_queue_node);
        }
    }

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_group_id_alloc
 Description  : group id alloc from opf pool
 Input        : uint8 lchip
 Output       : uint16 *group_id
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/22
 Modification : Created function

*****************************************************************************/
STATIC int32
_sys_greatbelt_group_id_alloc(uint8 lchip,
                              sys_queue_info_t* p_queue_info)
{
    uint32 group_id = 0;
    sys_greatbelt_opf_t opf;
    uint8 group_num = 1;
    uint16 index = 0;
    sys_queue_group_node_t* p_sys_group_node = NULL;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE_GROUP;

    if (p_queue_info->stacking_grp_en)
    {
        opf.multiple = 2;
        group_num = 4;
        p_queue_info->grp_bonding_en = 1;
    }
    else if (p_queue_info->grp_bonding_en)
    {
        opf.multiple = 2;
        group_num = 2;
    }
    CTC_ERROR_RETURN(sys_greatbelt_opf_alloc_offset(lchip, &opf, group_num, &group_id));

    p_queue_info->group_id = group_id;
    p_queue_info->bond_group = p_queue_info->grp_bonding_en? (group_id + 1):0;

    for (index = group_id; index < group_id + group_num; index++)
    {
        p_sys_group_node = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_group_node_t));
        if (NULL == p_sys_group_node)
        {
            sys_greatbelt_opf_free_offset(lchip, &opf, group_num, group_id);
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_sys_group_node, 0, sizeof(sys_queue_group_node_t));
        p_sys_group_node->p_queue_list = ctc_slist_new();
        p_sys_group_node->group_bond_en = p_queue_info->grp_bonding_en;

        p_sys_group_node->bond_group = (index % 2)?(index - 1):(index + 1);

        p_sys_group_node->group_id   = index;
        ctc_vector_add(p_gb_queue_master[lchip]->group_vec, index, p_sys_group_node);

        SYS_QUEUE_DBG_INFO("Group id alloc :%d\n",  index);
    }
    if (p_queue_info->stacking_grp_en)
    {
        CTC_ERROR_RETURN(sys_greatbelt_queue_sch_set_c2c_group_sched(lchip, p_queue_info->group_id, 2));
        CTC_ERROR_RETURN(sys_greatbelt_queue_sch_set_c2c_group_sched(lchip, p_queue_info->group_id + 2, 3));
    }
    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_group_id_free
 Description  : group id free from opf pool
 Input        : uint8 lchip
                uint16 group_id
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/22
 Modification : Created function

*****************************************************************************/
STATIC int32
_sys_greatbelt_group_id_free(uint8 lchip,
                             sys_queue_info_t* p_queue_info)
{
    sys_greatbelt_opf_t opf;
    uint32 group_id = 0;
    uint16 index = 0;
    uint8 group_num = 1;
    sys_queue_group_node_t* p_sys_group_node = NULL;

    SYS_QUEUE_DBG_FUNC();

    group_id = p_queue_info->group_id;

    if (p_queue_info->stacking_grp_en)
    {
        group_num = 4;
        CTC_ERROR_RETURN(sys_greatbelt_queue_sch_set_c2c_group_sched(lchip, p_queue_info->group_id + 2, 2));
    }
    else if (p_queue_info->grp_bonding_en)
    {
        group_num = 2;
    }
    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE_GROUP;
    CTC_ERROR_RETURN(sys_greatbelt_opf_free_offset(lchip, &opf, group_num, group_id));

    for (index = group_id; index < group_id + group_num; index++)
    {
        p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, index);
        if (NULL != p_sys_group_node)
        {
            ctc_slist_free(p_sys_group_node->p_queue_list);
            ctc_vector_del(p_gb_queue_master[lchip]->group_vec, index);
            mem_free(p_sys_group_node);
        }

        SYS_QUEUE_DBG_INFO("Group id free :%d\n",  index);

    }

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_group_rsv_for_cpu_reason
 Description  : reserve 16 group for cpu reason
 Input        : uint8 lchip
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/22
 Modification : Created function

*****************************************************************************/
int32
_sys_greatbelt_group_rsv_for_cpu_reason(uint8 lchip)
{
    sys_greatbelt_opf_t opf;
    uint32 offset = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));
    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE_GROUP;
    opf.reverse = 1;
    sys_greatbelt_opf_alloc_offset(lchip, &opf, SYS_QUEUE_MAX_GRP_NUM_FOR_CPU_REASON, &offset);

    p_gb_queue_master[lchip]->reason_grp_start = offset;
    SYS_QUEUE_DBG_INFO("Cpu reason group start:%d\n",  p_gb_queue_master[lchip]->reason_grp_start);

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_add_queue_key
 Description  : add queue hash key to asic
 Input        : uint8 lchip
                ds_queue_hash_key_t* p_ds_key
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/22
 Modification : Created function

*****************************************************************************/
int32
_sys_greatbelt_queue_add_queue_key(uint8 lchip,
                                   ds_queue_hash_key_t* p_ds_key)
{
    hash_io_by_key_para_t hash_io_para;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&hash_io_para, 0, sizeof(hash_io_by_key_para_t));

    hash_io_para.chip_id = lchip;
    hash_io_para.hash_module = HASH_MODULE_QUEUE;
    hash_io_para.key = (uint32*)p_ds_key;

    CTC_ERROR_RETURN(DRV_HASH_KEY_IOCTL(&hash_io_para, HASH_OP_TP_ADD_BY_KEY, NULL));

    return CTC_E_NONE;
}

/*****************************************************************************
 Prototype    : _sys_greatbelt_queue_remove_queue_key
 Description  : remove queue hash key from asic
 Input        : uint8 lchip
                ds_queue_hash_key_t* p_ds_key
 Output       : None
 Return Value : CTC_E_XXX
 History      :
 Date         : 2012/12/22
 Modification : Created function

*****************************************************************************/
int32
_sys_greatbelt_queue_remove_queue_key(uint8 lchip,
                                      ds_queue_hash_key_t* p_ds_key)
{
    hash_io_by_key_para_t hash_io_para;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&hash_io_para, 0, sizeof(hash_io_by_key_para_t));

    hash_io_para.chip_id = lchip;
    hash_io_para.hash_module = HASH_MODULE_QUEUE;
    hash_io_para.key = (uint32*)p_ds_key;

    CTC_ERROR_RETURN(DRV_HASH_KEY_IOCTL(&hash_io_para, HASH_OP_TP_DEL_BY_KEY, NULL));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_queue_update_queue_key(uint8 lchip,
                                   ds_queue_hash_key_t* p_ds_key, uint16 queue_base)
{
    hash_io_by_key_para_t hash_io_para;
    hash_io_rslt_t hash_io_rslt;
    ds_queue_hash_key_t queue_key;
    uint32 table_index = 0;
    uint32 cmd = 0;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue_key, 0, sizeof(ds_queue_hash_key_t));
    sal_memset(&hash_io_para, 0, sizeof(hash_io_by_key_para_t));
    sal_memset(&hash_io_rslt, 0, sizeof(hash_io_rslt_t));

    hash_io_para.chip_id = lchip;
    hash_io_para.hash_module = HASH_MODULE_QUEUE;
    hash_io_para.key = (uint32*)p_ds_key;

    CTC_ERROR_RETURN(DRV_HASH_KEY_IOCTL(&hash_io_para, HASH_OP_TP_LKUP_BY_KEY, &hash_io_rslt));

    table_index = hash_io_rslt.key_index;
    cmd = DRV_IOR(DsQueueHashKey_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, table_index, cmd, &queue_key));

    if((queue_key.valid0)
            && (queue_key.dest_chip_id0 == p_ds_key->dest_chip_id0)
            && (queue_key.dest_queue0 == p_ds_key->dest_queue0)
            && (queue_key.hash_type0 == p_ds_key->hash_type0))
    {
        queue_key.queue_base0 = queue_base;
    }
    else if((queue_key.valid1)
            && (queue_key.dest_chip_id1 == p_ds_key->dest_chip_id0)
            && (queue_key.dest_queue1 == p_ds_key->dest_queue0)
            && (queue_key.hash_type1 == p_ds_key->hash_type0))
    {
        queue_key.queue_base1 = queue_base;
    }
    else if((queue_key.valid2)
            && (queue_key.dest_chip_id2 == p_ds_key->dest_chip_id0)
            && (queue_key.dest_queue2 == p_ds_key->dest_queue0)
            && (queue_key.hash_type2 == p_ds_key->hash_type0))
    {
        queue_key.queue_base2 = queue_base;
    }
    else if((queue_key.valid3)
            && (queue_key.dest_chip_id3 == p_ds_key->dest_chip_id0)
            && (queue_key.dest_queue3 == p_ds_key->dest_queue0)
            && (queue_key.hash_type3 == p_ds_key->hash_type0))
    {
        queue_key.queue_base3 = queue_base;
    }

    cmd = DRV_IOW(DsQueueHashKey_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, table_index, cmd, &queue_key));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_service_update_queue(uint8 lchip, sys_queue_info_t* p_queue_info, uint8 b_add)
{
    ds_queue_hash_key_t queue_key;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&queue_key, 0, sizeof(ds_queue_hash_key_t));
    queue_key.hash_type0    = (1 | 1 << 3);
    queue_key.hash_type1    = queue_key.hash_type0;
    queue_key.service_id0 = p_queue_info->service_id;
    queue_key.dest_queue0 = p_queue_info->dest_queue;
    queue_key.queue_base0 = p_queue_info->queue_base;
    queue_key.valid0        = 1;

    if(b_add)
    {
        /*write key*/
        CTC_ERROR_RETURN(_sys_greatbelt_queue_add_queue_key(lchip, &queue_key));
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_queue_remove_queue_key(lchip, &queue_key));
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_check_queue_status(uint8 lchip, uint16 que_id)
{
    uint32 depth                   = 0;
    uint32 index                   = 0;
    uint32 cmd = 0;
    /*check queue flush clear*/
    cmd = DRV_IOR(DsQueCnt_t,DsQueCnt_QueInstCnt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, que_id, cmd, &depth));
    while (depth)
    {
        sal_task_sleep(20);
        if ((index++) > 1000)
        {
            return CTC_E_INVALID_PARAM;
        }
        cmd = DRV_IOR(DsQueCnt_t,DsQueCnt_QueInstCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, que_id, cmd, &depth));
    }   
    return CTC_E_NONE;
}
STATIC int32
_sys_greatbelt_queue_flush(uint8 lchip, sys_queue_info_t* p_queue_info, bool enable)
{
    uint8 queue_num     = 0;
    uint16 queue_base   = 0;
    uint8 cnt           = 0;
    uint32 cmd_flush    = 0;
    uint32 cmd_que_drop = 0;
    uint32 cmd_group_flush = 0;
    uint32 value        = 0;
    uint16 queue_id     = 0;
    uint16 group_id     = 0;

    uint32 profile_id   = 0;

    queue_base  = p_queue_info->queue_base;
    queue_num   = p_queue_info->queue_num;
    group_id    = p_queue_info->group_id;
    cmd_flush       = DRV_IOW(DsQueShpWfqCtl_t, DsQueShpWfqCtl_QueFlushValid_f);
    cmd_que_drop    = DRV_IOW(DsEgrResrcCtl_t, DsEgrResrcCtl_QueThrdProfIdHigh_f);
    value = enable ? 1 : 0;
    profile_id = enable? SYS_MAX_TAIL_DROP_PROFILE:SYS_DEFAULT_TAIL_DROP_PROFILE;
    for(cnt = 0; cnt < queue_num; cnt++)
    {
        queue_id = queue_base + cnt;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd_flush, &value));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd_que_drop, &profile_id));
    }
    cmd_group_flush = DRV_IOW(DsGrpShpWfqCtl_t, DsGrpShpWfqCtl_GrpFlushValid_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd_group_flush, &value));

    return CTC_E_NONE;
}

/*uint s*/
STATIC int32
_sys_greatbelt_queue_aging(uint8 lchip, uint32 interval)
{

    uint32 cmd = 0;
    uint32 core_freq = 0;
    q_mgr_aging_ctl_t q_mgr_aging_ctl;

    core_freq = sys_greatbelt_get_core_freq(lchip);
    sal_memset(&q_mgr_aging_ctl, 0 , sizeof(q_mgr_aging_ctl_t));

    cmd = DRV_IOR(QMgrAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_aging_ctl));
    q_mgr_aging_ctl.aging_en        = interval ? 1 :0;
    q_mgr_aging_ctl.aging_max_ptr   = (SYS_MAX_QUEUE_ENTRY_NUM -1);
    q_mgr_aging_ctl.aging_interval  = (interval * core_freq * 1000000)/(12*1024*3) -1;

    cmd = DRV_IOW(QMgrAgingCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_aging_ctl));


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_default(uint8 lchip, uint16 queue_base, uint8 queue_offset, uint16 group_id)
{
    uint32 cmd = 0;
    uint16 queue_id = 0;
    ds_que_shp_ctl_t ds_que_shp_ctl;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;
    ds_que_shp_wfq_ctl_t ds_que_shp_wfq_ctl;

    queue_id = queue_base + queue_offset;
    /*queue shape*/
    cmd = DRV_IOR(DsQueShpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_que_shp_ctl));
    CTC_BIT_UNSET(ds_que_shp_ctl.que_shp_en_vec, queue_offset);
    cmd = DRV_IOW(DsQueShpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_que_shp_ctl));

    /*queue class*/
    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

    ds_grp_shp_wfq_ctl.que_offset0_green_remap_sch_pri = 0;
    ds_grp_shp_wfq_ctl.que_offset1_green_remap_sch_pri = 1;
    ds_grp_shp_wfq_ctl.que_offset2_green_remap_sch_pri = 2;
    ds_grp_shp_wfq_ctl.que_offset3_green_remap_sch_pri = 3;
    ds_grp_shp_wfq_ctl.que_offset4_green_remap_sch_pri = 4;
    ds_grp_shp_wfq_ctl.que_offset5_green_remap_sch_pri = 5;
    ds_grp_shp_wfq_ctl.que_offset6_green_remap_sch_pri = 6;
    ds_grp_shp_wfq_ctl.que_offset7_green_remap_sch_pri = 7;

    ds_grp_shp_wfq_ctl.que_offset0_yellow_remap_sch_pri = 0;
    ds_grp_shp_wfq_ctl.que_offset1_yellow_remap_sch_pri = 1;
    ds_grp_shp_wfq_ctl.que_offset2_yellow_remap_sch_pri = 2;
    ds_grp_shp_wfq_ctl.que_offset3_yellow_remap_sch_pri = 3;
    ds_grp_shp_wfq_ctl.que_offset4_yellow_remap_sch_pri = 4;
    ds_grp_shp_wfq_ctl.que_offset5_yellow_remap_sch_pri = 5;
    ds_grp_shp_wfq_ctl.que_offset6_yellow_remap_sch_pri = 6;
    ds_grp_shp_wfq_ctl.que_offset7_yellow_remap_sch_pri = 7;
    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

    /*Queue weight*/
    cmd = DRV_IOR(DsQueShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_shp_wfq_ctl));
    ds_que_shp_wfq_ctl.cir_weight       = SYS_WFQ_DEFAULT_WEIGHT;
    ds_que_shp_wfq_ctl.cir_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
    ds_que_shp_wfq_ctl.pir_weight       = SYS_WFQ_DEFAULT_WEIGHT;
    ds_que_shp_wfq_ctl.pir_weight_shift = SYS_WFQ_DEFAULT_SHIFT;
    cmd = DRV_IOW(DsQueShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_shp_wfq_ctl));


    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_group_default(uint8 lchip, uint16 group_id)
{

    uint32 cmd = 0;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;


    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
    /*Group shape*/
    ds_grp_shp_wfq_ctl.grp_shp_en           = 1;
    ds_grp_shp_wfq_ctl.grp_shp_prof_id      = 0;
    /*Group class and weight*/
    ds_grp_shp_wfq_ctl.weight               = SYS_WFQ_DEFAULT_WEIGHT;
    ds_grp_shp_wfq_ctl.weight_shift         = SYS_WFQ_DEFAULT_SHIFT;

    ds_grp_shp_wfq_ctl.grp_sch_pri0_remap_ch_pri = 0;
    ds_grp_shp_wfq_ctl.grp_sch_pri1_remap_ch_pri = 0;
    ds_grp_shp_wfq_ctl.grp_sch_pri2_remap_ch_pri = 1;
    ds_grp_shp_wfq_ctl.grp_sch_pri3_remap_ch_pri = 1;
    ds_grp_shp_wfq_ctl.grp_sch_pri4_remap_ch_pri = 2;
    ds_grp_shp_wfq_ctl.grp_sch_pri5_remap_ch_pri = 2;
    ds_grp_shp_wfq_ctl.grp_sch_pri6_remap_ch_pri = 3;
    ds_grp_shp_wfq_ctl.grp_sch_pri7_remap_ch_pri = 3;

    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_add(uint8 lchip, sys_queue_info_t* p_queue_info)
{

    ds_queue_hash_key_t queue_key;
    ds_queue_num_gen_ctl_t ds_queue_num_gen_ctl;
    uint8 index[80]  = {0};
    uint32 cmd        = 0;
    uint8 qsel_shift  = 0;
    uint8 offset      = 0;
    uint16 queue_id   = 0;
    uint16 group_id   = 0;
    uint8 idx         = 0;
    uint8 i           = 0;
    uint8 queue_type  = 0;
    uint8 queue_num   = 0;
    uint16 queue_base = 0;
    uint8 chan_id     = 0;
    uint16 dest_queue = 0;
    uint8 que_sel_type= 0;

    SYS_QUEUE_DBG_FUNC();

    queue_type = p_queue_info->queue_type;
    queue_num  = p_queue_info->queue_num;
    queue_base = p_queue_info->queue_base;
    dest_queue = p_queue_info->dest_queue;
    group_id   = p_queue_info->group_id;

    sal_memset(&queue_key, 0, sizeof(ds_queue_hash_key_t));
    sal_memset(&ds_queue_num_gen_ctl, 0, sizeof(ds_queue_num_gen_ctl_t));

    switch (queue_type)
    {
    case SYS_QUEUE_TYPE_NORMAL:
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        ds_queue_num_gen_ctl.dest_queue_en      = 1;
        ds_queue_num_gen_ctl.dest_queue_mask    = 0x0FFF;

        /*from local to local (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 1, SYS_QSEL_TYPE_REGULAR);

        /*from uplink to local (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_REGULAR);

        /*from uplink to local (mirror)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_MIRROR);

        /*from uplink to local (mcast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 1, 0, 1, SYS_QSEL_TYPE_MCAST);

        break;

    case SYS_QUEUE_TYPE_INTERNAL:
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        ds_queue_num_gen_ctl.dest_queue_en      = 1;
        ds_queue_num_gen_ctl.dest_queue_mask    = 0xFFFF;

        /*from local to local (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 1, SYS_QSEL_TYPE_INTERNAL_PORT);

        /*from uplink to local (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_INTERNAL_PORT);
        break;

    case SYS_QUEUE_TYPE_EXCP:
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        ds_queue_num_gen_ctl.dest_queue_en      = 1;
        ds_queue_num_gen_ctl.dest_queue_mask    = 0xFFFF;

        /*from local to local cpu(ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 1, SYS_QSEL_TYPE_EXCP_CPU);

        /*from uplink to local cpu(ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_EXCP_CPU);

        break;


    case SYS_QUEUE_TYPE_SERVICE:
        queue_key.hash_type0    = (1 | 1<<3);
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.service_id0 = p_queue_info->service_id;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        ds_queue_num_gen_ctl.dest_queue_en      = 1;
        ds_queue_num_gen_ctl.dest_queue_mask    = 0xFF;
        ds_queue_num_gen_ctl.service_queue_en   = 1;
        ds_queue_num_gen_ctl.service_id_mask    = 0xFFFF;

        /*from local to local using QueSelType (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 1, SYS_QSEL_TYPE_SERVICE);

        /*from uplink to local ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_SERVICE);

        /*from local to local using SrcQueSel*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 1, 1, SYS_QSEL_TYPE_REGULAR);

        break;




    case SYS_QUEUE_TYPE_STACKING:                /* stacking queue */
        chan_id = p_queue_info->channel;

        queue_key.hash_type0    = 4;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.sgmac0        = chan_id;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        ds_queue_num_gen_ctl.sgmac_en = 1;
        ds_queue_num_gen_ctl.sgmac_mask = 0x3F;

        /*DestChip not match, stcking*/
        for (que_sel_type = 0; que_sel_type <= 0x1F;  que_sel_type++)
        {
            if ((SYS_QSEL_TYPE_C2C == que_sel_type)||(SYS_QSEL_TYPE_CPU_TX == que_sel_type))
            {
                continue;
            }
            index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 0, que_sel_type);
            index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 0, que_sel_type);
        }

        /*from local to uplink (mcast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 1, 0, 0, SYS_QSEL_TYPE_MCAST);

        /*from uplink to uplink (mcast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 1, 0, 0, SYS_QSEL_TYPE_MCAST);
        break;

    case SYS_QUEUE_TYPE_CPU_TX :                /* stacking queue */
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue | (SYS_QSEL_TYPE_CPU_TX << 13);
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        ds_queue_num_gen_ctl.dest_queue_en      = 1;
        ds_queue_num_gen_ctl.dest_queue_mask    = 0xFFFF;

        /*from local to local (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 1, SYS_QSEL_TYPE_CPU_TX);

         /*from local to uplink (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 0, SYS_QSEL_TYPE_CPU_TX);

        /*from uplink to local (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_CPU_TX);

        /*from uplink to uplink (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 0, SYS_QSEL_TYPE_CPU_TX);

        break;

    case SYS_QUEUE_TYPE_C2C :                /* stacking queue */
        chan_id = p_queue_info->channel;
        queue_key.hash_type0    = 5;
        queue_key.hash_type1    = queue_key.hash_type0;
        /*SYS_QSEL_TYPE_C2C << 13 | 1 mean c2c pkt in trunk port*/
        queue_key.dest_queue0   = SYS_QSEL_TYPE_C2C << 13 | 0x10;
        queue_key.sgmac0        = chan_id;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;
        ds_queue_num_gen_ctl.dest_queue_en      = 1;
        ds_queue_num_gen_ctl.dest_queue_mask    = 0xFF70;
        ds_queue_num_gen_ctl.sgmac_en = 1;
        ds_queue_num_gen_ctl.sgmac_mask = 0x3F;

        /*from local to uplink (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(0, 0, 0, 0, SYS_QSEL_TYPE_C2C);
        /*from uplink to uplink (ucast)*/
        index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 0, SYS_QSEL_TYPE_C2C);

        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    p_queue_info->channel = chan_id;

    /*write queue chan mapping*/
    /*For cpu reason have process queue/group/channel*/
    if (SYS_QUEUE_TYPE_EXCP != queue_type)
    {
        for (offset = 0; offset < queue_num; offset++)
        {
            queue_id = queue_base + offset;
            if (offset >= 8)
            {
                group_id = p_queue_info->group_id + 2;
            }
            CTC_ERROR_RETURN(sys_greatbelt_queue_add_to_channel(lchip, queue_id, chan_id));
            CTC_ERROR_RETURN(sys_greatbelt_queue_add_to_group(lchip, queue_id, group_id));
        }
    }

    CTC_ERROR_RETURN(sys_greatbelt_queue_set_default_drop(lchip, queue_id, SYS_DEFAULT_TAIL_DROP_PROFILE));

    /*write key*/
    SYS_QUEUE_DBG_INFO("queue_key.hash_type0:%d\n",  queue_key.hash_type0);
    SYS_QUEUE_DBG_INFO("queue_key.hash_type1:%d\n",  queue_key.hash_type1);
    SYS_QUEUE_DBG_INFO("queue_key.dest_queue0:%d\n",  queue_key.dest_queue0);
    SYS_QUEUE_DBG_INFO("queue_key.queue_base0:%d\n",  queue_key.queue_base0);
    SYS_QUEUE_DBG_INFO("queue_key.sgmac0:%d\n",  queue_key.sgmac0);
    SYS_QUEUE_DBG_INFO("queue_key.valid0:%d\n",       queue_key.valid0);
    if (!(SYS_QUEUE_TYPE_INTERNAL == queue_type && SYS_OAM_CHANNEL_ID == chan_id))
    {   /* not write hash key with oam channel*/
        CTC_ERROR_RETURN(_sys_greatbelt_queue_add_queue_key(lchip, &queue_key));
    }

    /*write queue number gen ctl*/
    _sys_greatbelt_queue_get_qsel_shift(lchip, queue_num, &qsel_shift);
    SYS_QUEUE_DBG_INFO("queue_num:%d,  qsel_shift:%d\n", queue_num, qsel_shift);

    ds_queue_num_gen_ctl.queue_select_shift = qsel_shift;
    ds_queue_num_gen_ctl.queue_gen_mode     = 1;
    ds_queue_num_gen_ctl.flow_id_mask       = 0;
    cmd = DRV_IOW(DsQueueNumGenCtl_t, DRV_ENTRY_FLAG);

    for (i = 0; i < idx; i++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index[i], cmd, &ds_queue_num_gen_ctl));
    }

    if (SYS_QUEUE_TYPE_INTERNAL == queue_type
        && SYS_OAM_CHANNEL_ID == chan_id)
    {
        q_write_ctl_t q_write_ctl;
        sal_memset(&q_write_ctl, 0, sizeof(q_write_ctl_t));
        cmd = DRV_IOR(QWriteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));

        /*for oam enqueue*/
        q_write_ctl.gen_que_id_rx_ether_oam         = 1;
        q_write_ctl.rx_ether_oam_queue_select_shift = qsel_shift;
        q_write_ctl.rx_ether_oam_queue_base0        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base1        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base2        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base3        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base4        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base5        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base6        = queue_base;
        q_write_ctl.rx_ether_oam_queue_base7        = queue_base;

        cmd = DRV_IOW(QWriteCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_write_ctl));
    }

    return DRV_E_NONE;
}



STATIC int32
_sys_greatbelt_queue_remove(uint8 lchip, sys_queue_info_t* p_queue_info)
{
    ds_queue_hash_key_t queue_key;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;
    ds_queue_num_gen_ctl_t ds_queue_num_gen_ctl;
    uint8 offset;
    uint16 queue_id;
    uint16 group_id;
    uint8 queue_type;
    uint8 queue_num;
    uint16 queue_base;
    uint16 dest_queue;
    uint8 chan_id     = 0;
    uint8 cnt = 0;
    uint32 cmd = 0;
    int32 ret = 0;
    uint32  field_val = 0;
    uint32 token_rate = 0;
    uint32 token_thrd = 0;
    uint32 token_thrd_shift = 0;
    ds_chan_shp_profile_t ds_profile;

    SYS_QUEUE_DBG_FUNC();

    queue_type = p_queue_info->queue_type;
    queue_num  = p_queue_info->queue_num;
    queue_base = p_queue_info->queue_base;
    dest_queue = p_queue_info->dest_queue;
    group_id   = p_queue_info->group_id;

    sal_memset(&queue_key, 0, sizeof(ds_queue_hash_key_t));
    sal_memset(&ds_grp_shp_wfq_ctl, 0, sizeof(ds_grp_shp_wfq_ctl_t));
    sal_memset(&ds_queue_num_gen_ctl, 0, sizeof(ds_queue_num_gen_ctl_t));

    switch (queue_type)
    {
    case SYS_QUEUE_TYPE_NORMAL:
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = dest_queue;

        break;

    case SYS_QUEUE_TYPE_INTERNAL:
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        break;

    case SYS_QUEUE_TYPE_EXCP:
        queue_key.hash_type0    = 1;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;

        break;


    case SYS_QUEUE_TYPE_SERVICE:
        queue_key.hash_type0    = (1 | 1<<3);
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.service_id0 = p_queue_info->service_id;
        queue_key.dest_queue0 = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = p_queue_info->channel;
        break;

    case SYS_QUEUE_TYPE_STACKING:                /* fabric queue */
        queue_key.hash_type0    = 4;
        queue_key.hash_type1    = queue_key.hash_type0;
        queue_key.sgmac0        = dest_queue;
        queue_key.queue_base0 = queue_base;
        queue_key.valid0        = 1;

        chan_id = dest_queue;

        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    SYS_QUEUE_DBG_INFO("queue_key.hash_type0:%d\n",  queue_key.hash_type0);
    SYS_QUEUE_DBG_INFO("queue_key.hash_type1:%d\n",  queue_key.hash_type1);
    SYS_QUEUE_DBG_INFO("queue_key.dest_queue0:%d\n",  queue_key.dest_queue0);
    SYS_QUEUE_DBG_INFO("queue_key.queue_base0:%d\n",  queue_key.queue_base0);
    SYS_QUEUE_DBG_INFO("queue_key.sgmac0:%d\n",  queue_key.sgmac0);
    SYS_QUEUE_DBG_INFO("queue_key.valid0:%d\n",       queue_key.valid0);
    if (!(SYS_QUEUE_TYPE_INTERNAL == queue_type && SYS_OAM_CHANNEL_ID == chan_id))
    {   /* not write hash key with oam channel*/
        CTC_ERROR_RETURN(_sys_greatbelt_queue_remove_queue_key(lchip, &queue_key));
    }

    /*For cpu reason have process queue/group/channel*/
    if (SYS_QUEUE_TYPE_EXCP != queue_type)
    {
        cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
        /*Group shape*/
        ds_grp_shp_wfq_ctl.grp_shp_en           = 0;
        /*Group class and weight*/
        ds_grp_shp_wfq_ctl.weight               = SYS_WFQ_DEFAULT_WEIGHT;
        ds_grp_shp_wfq_ctl.weight_shift         = SYS_WFQ_DEFAULT_SHIFT;
        ds_grp_shp_wfq_ctl.grp_sch_pri0_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri1_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri2_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri3_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri4_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri5_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri6_remap_ch_pri = 3;
        ds_grp_shp_wfq_ctl.grp_sch_pri7_remap_ch_pri = 3;
        cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
        /* disable channel shaping */
        sal_memset(&ds_profile, 0, sizeof(ds_chan_shp_profile_t));
        cmd = DRV_IOR(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &ds_profile),ret,error0);
        token_rate = ds_profile.token_rate;
        token_thrd = ds_profile.token_thrd ;
        token_thrd_shift = ds_profile.token_thrd_shift;
        ds_profile.token_rate = SYS_SHAPE_MAX_TOKEN_RATE;
        ds_profile.token_thrd = SYS_SHAPE_MAX_TOKEN_THRD;
        ds_profile.token_thrd_shift = SYS_SHAPE_MAX_TOKEN_THRD_SHIFT;
        cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &ds_profile),ret,error0);

        /* disable queue shaping */
        field_val = 0;
        cmd = DRV_IOW(DsQueShpCtl_t, DsQueShpCtl_QueShpEnVec_f);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_queue_info->group_id, cmd, &field_val),ret,error1); 
        for(cnt = 0; cnt < p_queue_info->queue_num; cnt++)
        {
            CTC_ERROR_GOTO(_sys_greatbelt_check_queue_status(lchip,p_queue_info->queue_base+cnt),ret,error1);
        }

        /* enable channel shaping */
        sal_memset(&ds_profile, 0, sizeof(ds_chan_shp_profile_t));
        cmd = DRV_IOR(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &ds_profile),ret,error1);
        ds_profile.token_rate = token_rate;
        ds_profile.token_thrd = token_thrd;
        ds_profile.token_thrd_shift = token_thrd_shift;     
        cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, chan_id, cmd, &ds_profile),ret,error1);
            
        for (offset = 0; offset < queue_num; offset++)
        {
            queue_id = queue_base + offset;
            if (offset >= 8)
            {
                group_id = p_queue_info->group_id + 2;
            }
            CTC_ERROR_GOTO(sys_greatbelt_queue_remove_from_group(lchip, queue_id, group_id),ret,error0);
            CTC_ERROR_GOTO(sys_greatbelt_queue_remove_from_channel(lchip, queue_id, chan_id),ret,error0);
            CTC_ERROR_GOTO(_sys_greatbelt_queue_default(lchip, queue_base, offset, group_id),ret,error0);
        }
        CTC_ERROR_GOTO(_sys_greatbelt_group_default(lchip, group_id),ret,error0);
    }

    return DRV_E_NONE;
error1:
    sal_memset(&ds_profile, 0, sizeof(ds_chan_shp_profile_t));
    cmd = DRV_IOR(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, chan_id, cmd, &ds_profile);
    ds_profile.token_rate = token_rate;
    ds_profile.token_thrd = token_thrd;
    ds_profile.token_thrd_shift = token_thrd_shift;
    cmd = DRV_IOW(DsChanShpProfile_t, DRV_ENTRY_FLAG);
    DRV_IOCTL(lchip, chan_id, cmd, &ds_profile);
error0:
    _sys_greatbelt_group_default(lchip, group_id);    
    return ret;
}

STATIC int32
_sys_greatbelt_port_queue_remove(uint8 lchip,
                                uint8 queue_type,
                                uint8 lport)
{
    sys_queue_info_t* p_queue_info = NULL;
    SYS_QUEUE_DBG_FUNC();

    p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;

    CTC_ERROR_RETURN(_sys_greatbelt_queue_remove(lchip, p_queue_info));

    /*free old normal queue*/
    CTC_ERROR_RETURN(_sys_greatbelt_group_id_free(lchip,
                                                  p_queue_info));

    /*free old normal queue*/
    CTC_ERROR_RETURN(_sys_greatbelt_queue_id_free(lchip,
                                                  p_queue_info->queue_num,
                                                  p_queue_info->queue_base));

    sal_memset(p_queue_info, 0, sizeof(sys_queue_info_t));

    return CTC_E_NONE;

}


int32
sys_greatbelt_port_queue_remove(uint8 lchip,
                                uint8 queue_type,
                                uint8 lport)
{
    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
        _sys_greatbelt_port_queue_remove(lchip, queue_type, lport));
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_port_queue_add(uint8 lchip,
                             uint8 queue_type,
                             uint8 lport,
                             uint8 channel)
{
    uint16 queue_base = 0;
    uint8 queue_num = 0;
    uint8 chan_id = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    sys_queue_info_t* p_queue_info = NULL;

    SYS_QUEUE_DBG_FUNC();

    /* get channel id */
    if (queue_type == SYS_QUEUE_TYPE_NORMAL || queue_type == SYS_QUEUE_TYPE_STACKING )
    {
        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    }
    else
    {
        chan_id = channel;
    }

    if (0xFF == chan_id)
    {
        /* do not allocate queue if lport is not valid */
        return CTC_E_NONE;
    }

    queue_num = p_gb_queue_master[lchip]->queue_num_ctl[queue_type].queue_num;
    p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;

    if (p_queue_info->queue_type == queue_type
        && p_queue_info->queue_num == queue_num)
    {
        return CTC_E_NONE;
    }

    if (p_queue_info->queue_num)
    {
        _sys_greatbelt_port_queue_remove(lchip, queue_type, lport);
    }

    /*group bonding enable*/
    if (queue_num > 4)
    {
        p_queue_info->grp_bonding_en = 1;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_group_id_alloc(lchip, p_queue_info));

    /* Alloc new queue for network queue, network channel from 0 - 55 */
    CTC_ERROR_RETURN(_sys_greatbelt_queue_id_alloc(lchip, queue_num, &queue_base));

    p_queue_info->queue_type = queue_type;
    p_queue_info->queue_base = queue_base;
    p_queue_info->queue_num  = queue_num;
    p_queue_info->dest_queue = lport;
    p_queue_info->channel    = chan_id;

    CTC_ERROR_RETURN(_sys_greatbelt_queue_add(lchip, p_queue_info));

    return CTC_E_NONE;
}


int32
sys_greatbelt_port_queue_add(uint8 lchip,
                             uint8 queue_type,
                             uint8 lport,
                             uint8 channel)
{

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
        _sys_greatbelt_port_queue_add(lchip, queue_type, lport, channel));
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_port_queue_add_for_stacking(uint8 lchip,
                             uint8 queue_type,
                             uint8 lport,
                             uint8 channel)
{
    uint16 queue_base = 0;
    uint8 queue_num = 0;
    uint8 chan_id = 0;
    uint16 gport = 0;
    uint8 gchip = 0;
    uint8 queue_offset = 0;
    uint16 stacking_queue_base = 0;
    uint16 group_id   = 0;
    sys_queue_info_t* p_queue_info = NULL;
    ds_queue_hash_key_t queue_key;

    SYS_QUEUE_DBG_FUNC();

    /* get channel id */
    if (queue_type == SYS_QUEUE_TYPE_NORMAL || queue_type == SYS_QUEUE_TYPE_STACKING )
    {
        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
        chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    }
    else
    {
        chan_id = channel;
    }

    if (0xFF == chan_id)
    {
        /* do not allocate queue if lport is not valid */
        return CTC_E_NONE;
    }

    SYS_QOS_QUEUE_LOCK(lchip);
    queue_num = p_gb_queue_master[lchip]->queue_num_ctl[queue_type].queue_num;
    p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;
    stacking_queue_base = p_queue_info->queue_base;
    if (0 == p_queue_info->queue_num)
    {
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_CONFIG;
    }
    if (SYS_QUEUE_TYPE_STACKING == queue_type)/* add port to trunk*/
    {
        if (p_queue_info->stacking_trunk_ref_cnt)
        {
            goto REF_CNT;
        }
    }
    else/* remove port to trunk*/
    {
        if (p_queue_info->stacking_trunk_ref_cnt > 1)
        {
            goto REF_CNT;
        }
    }

    if (p_queue_info->queue_num)
    {
        if (p_gb_queue_master[lchip]->enq_mode)
        {
            /* remove normal/cpu_tx key*/
            sal_memset(&queue_key, 0, sizeof(ds_queue_hash_key_t));
            queue_key.hash_type0    = 1;
            queue_key.hash_type1    = queue_key.hash_type0;
            queue_key.dest_queue0 = p_queue_info->dest_queue;
            queue_key.queue_base0 = p_queue_info->queue_base;
            queue_key.valid0        = 1;
            _sys_greatbelt_queue_remove_queue_key(lchip, &queue_key);

            /* remove c2c key*/
            queue_key.hash_type0    = 5;
            queue_key.hash_type1    = queue_key.hash_type0;
            /*SYS_QSEL_TYPE_C2C << 13 | 1 mean c2c pkt in trunk port*/
            queue_key.dest_queue0   = SYS_QSEL_TYPE_C2C << 13 | 1;
            queue_key.sgmac0   = chan_id;
            _sys_greatbelt_queue_remove_queue_key(lchip, &queue_key);
        }

        /* remove stacking key and queue resource*/
        _sys_greatbelt_port_queue_remove(lchip, queue_type, lport);
    }

    if (SYS_QUEUE_TYPE_STACKING == queue_type)
    {
        p_queue_info->stacking_grp_en = 1;
    }
    else if (queue_num > 4)
    {
        p_queue_info->bond_group = 1;
    }

    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_group_id_alloc(lchip, p_queue_info));

    /* Alloc new queue for network queue, network channel from 0 - 55 */
    if (SYS_QUEUE_TYPE_STACKING == queue_type)
    {
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_id_alloc(lchip, 16, &queue_base));

        if (p_gb_queue_master[lchip]->egs_pool[CTC_QOS_EGS_RESRC_CONTROL_POOL].egs_congest_level_num)
        {
            for (queue_offset = 8; queue_offset < 16; queue_offset ++)
            {
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_queue_set_queue_resrc(lchip, queue_base + queue_offset, CTC_QOS_EGS_RESRC_CONTROL_POOL));
            }
        }
    }
    else
    {
        if (p_gb_queue_master[lchip]->egs_pool[CTC_QOS_EGS_RESRC_DEFAULT_POOL].egs_congest_level_num)
        {
            for (queue_offset = 8; queue_offset < 16; queue_offset ++)
            {
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_queue_set_queue_resrc(lchip, stacking_queue_base + queue_offset, CTC_QOS_EGS_RESRC_DEFAULT_POOL));
            }
        }

        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_id_alloc(lchip, queue_num, &queue_base));
    }

    p_queue_info->queue_type = queue_type;
    p_queue_info->queue_base = queue_base;
    p_queue_info->queue_num  = queue_num;
    p_queue_info->dest_queue = lport;
    p_queue_info->channel    = chan_id;
    group_id = p_queue_info->group_id;

    if (SYS_QUEUE_TYPE_STACKING == queue_type)/* for stacking c2c*/
    {
        p_queue_info->queue_type = SYS_QUEUE_TYPE_STACKING;
        p_queue_info->queue_num  = 8;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_add(lchip, p_queue_info));

        p_queue_info->queue_type = SYS_QUEUE_TYPE_NORMAL;
        p_queue_info->queue_num  = 8;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_add(lchip, p_queue_info));

        p_queue_info->queue_type = SYS_QUEUE_TYPE_CPU_TX;
        p_queue_info->queue_num  = 16;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_add(lchip, p_queue_info));

        p_queue_info->queue_type = SYS_QUEUE_TYPE_C2C;
        p_queue_info->queue_num  = p_gb_queue_master[lchip]->c2c_enq_mode ? 16 : 8;
        p_queue_info->queue_base = p_gb_queue_master[lchip]->c2c_enq_mode ? queue_base : (queue_base + 8);
        p_queue_info->group_id   = p_gb_queue_master[lchip]->c2c_enq_mode ? group_id : (group_id + 2);
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_add(lchip, p_queue_info));

        p_queue_info->queue_type = queue_type;
        p_queue_info->queue_num  = 16; /* save 16q per port to sw*/
        p_queue_info->queue_base = queue_base;
        p_queue_info->group_id   = group_id;
    }
    else
    {
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_add(lchip, p_queue_info));
    }


REF_CNT:
    if (SYS_QUEUE_TYPE_STACKING == queue_type)
    {
        p_queue_info->stacking_trunk_ref_cnt++;
    }
    else
    {
        if (p_queue_info->stacking_trunk_ref_cnt)
        {
            p_queue_info->stacking_trunk_ref_cnt--;
        }
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_port_queue_flush(uint8 lchip, uint8 lport, bool enable)
{
    sys_queue_info_t* p_queue_info = NULL;

    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_LOCK(lchip);
    p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return _sys_greatbelt_queue_flush(lchip, p_queue_info, enable);
}

int32
sys_greatbelt_port_queue_swap(uint8 lchip, uint8 lport1, uint8 lport2)
{

    sys_queue_info_t* p_queue_info1     = NULL;
    sys_queue_info_t* p_queue_info2     = NULL;
    sys_queue_info_t  queue_info_swap;

    ds_queue_hash_key_t queue_hash_key1;
    ds_queue_hash_key_t queue_hash_key2;

    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_LOCK(lchip);
    p_queue_info1 = &p_gb_queue_master[lchip]->channel[lport1].queue_info;
    p_queue_info2 = &p_gb_queue_master[lchip]->channel[lport2].queue_info;
    SYS_QOS_QUEUE_UNLOCK(lchip);

    sal_memset(&queue_hash_key1, 0, sizeof(ds_queue_hash_key_t));
    sal_memset(&queue_hash_key2, 0, sizeof(ds_queue_hash_key_t));

    queue_hash_key1.hash_type0    = 1;
    queue_hash_key1.hash_type1    = queue_hash_key1.hash_type0;
    queue_hash_key1.dest_queue0   = lport1;
    queue_hash_key1.queue_base0   = p_queue_info1->queue_base;
    queue_hash_key1.valid0        = 1;

    queue_hash_key2.hash_type0    = 1;
    queue_hash_key2.hash_type1    = queue_hash_key2.hash_type0;
    queue_hash_key2.dest_queue0   = lport2;
    queue_hash_key2.queue_base0   = p_queue_info2->queue_base;
    queue_hash_key2.valid0        = 1;

    _sys_greatbelt_queue_update_queue_key(lchip, &queue_hash_key1, p_queue_info2->queue_base);
    _sys_greatbelt_queue_update_queue_key(lchip, &queue_hash_key2, p_queue_info1->queue_base);

    SYS_QOS_QUEUE_LOCK(lchip);
    sal_memcpy(&queue_info_swap, p_queue_info2, sizeof(sys_queue_info_t));
    sal_memcpy(p_queue_info2, p_queue_info1, sizeof(sys_queue_info_t));
    sal_memcpy(p_queue_info1, &queue_info_swap, sizeof(sys_queue_info_t));
    p_queue_info1->dest_queue = lport1;
    p_queue_info2->dest_queue = lport2;
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_greatbelt_service_queue_remove(uint8 lchip,
                                uint16 service_id,
                                uint16 logic_port,
                                uint8 lport)
{
    uint8 queue_type = 0;

    sys_queue_info_t* p_queue_info = NULL;
    sys_queue_info_t queue_info;


    SYS_QUEUE_DBG_FUNC();

    queue_type = SYS_QUEUE_TYPE_SERVICE;

    sal_memset(&queue_info, 0, sizeof(sys_queue_info_t));
    queue_info.service_id = service_id;
    queue_info.dest_queue = lport;
    queue_info.queue_type = queue_type;
    p_queue_info = sys_greatbelt_queue_info_lookup(lchip, &queue_info);
    if (NULL == p_queue_info)
    {
        return CTC_E_NONE;
    }

    p_queue_info->service_id = logic_port;
    p_queue_info->ref_cnt--;
    if(p_queue_info->ref_cnt)
    {
        _sys_greatbelt_service_update_queue(lchip, p_queue_info, FALSE);
        p_queue_info->service_id = service_id;
    }
    else
    {
        CTC_ERROR_RETURN(_sys_greatbelt_queue_remove(lchip, p_queue_info));

        /*free old normal queue*/
        CTC_ERROR_RETURN(_sys_greatbelt_group_id_free(lchip,
                                                      p_queue_info));
        /*free old normal queue*/
        CTC_ERROR_RETURN(_sys_greatbelt_queue_id_free(lchip,
                                                      p_queue_info->queue_num,
                                                      p_queue_info->queue_base));
        p_queue_info->service_id = service_id;
        sys_greatbelt_queue_info_remove(lchip, p_queue_info);
        mem_free(p_queue_info);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_service_queue_add(uint8 lchip,
                                uint16 service_id,
                                uint16 logic_port,
                                uint8 lport)
{
    uint16 queue_base = 0;
    uint8 queue_num = 0;
    uint8 queue_type = 0;
    uint8 gchip = 0;
    uint16 gport = 0;
    uint8 chan_id = 0;
    uint8 shp_bucket_cir = 0;
    uint8 shp_bucket_pir = 0;
    uint32 cmd = 0;
    int32 ret = 0;

    sys_queue_info_t* p_queue_info = NULL;
    sys_queue_info_t queue_info;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;


    SYS_QUEUE_DBG_FUNC();

    queue_type = SYS_QUEUE_TYPE_SERVICE;

    queue_num = p_gb_queue_master[lchip]->queue_num_ctl[queue_type].queue_num;

    sal_memset(&queue_info, 0, sizeof(sys_queue_info_t));
    queue_info.service_id = service_id;
    queue_info.dest_queue = lport;
    queue_info.queue_type = queue_type;
    p_queue_info = sys_greatbelt_queue_info_lookup(lchip, &queue_info);
    if (NULL != p_queue_info)
    {
        /*write logic port queue*/
        p_queue_info->service_id = logic_port;
        p_queue_info->ref_cnt++;
        _sys_greatbelt_service_update_queue(lchip, p_queue_info, TRUE);
        p_queue_info->service_id = service_id;
        return CTC_E_NONE;
    }

    SYS_QUEUE_DBG_INFO("queue info not exist");
    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if(0xFF == chan_id)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    p_queue_info =  mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_info_t));
    if (NULL == p_queue_info)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_queue_info, 0, sizeof(sys_queue_info_t));

    /*using logic port enqueue*/
    p_queue_info->service_id = logic_port;
    p_queue_info->dest_queue = lport;
    p_queue_info->channel    = chan_id;

    /*group bonding enable*/
    if (queue_num > 4)
    {
        p_queue_info->grp_bonding_en = 1;
    }

    CTC_ERROR_GOTO(_sys_greatbelt_group_id_alloc(lchip, p_queue_info), ret, error_1);

    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_queue_info->group_id, cmd, &ds_grp_shp_wfq_ctl), ret, error_1);

    shp_bucket_cir = p_queue_info->grp_bonding_en ? SYS_SHAPE_BUCKET_CIR_FAIL1 :
    SYS_SHAPE_BUCKET_CIR_FAIL0;

    shp_bucket_pir = p_queue_info->grp_bonding_en ? SYS_SHAPE_BUCKET_PIR_PASS1 :
    SYS_SHAPE_BUCKET_PIR_PASS0;

    ds_grp_shp_wfq_ctl.que_offset0_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset1_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset2_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset3_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset4_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset5_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset6_sel_cir  = shp_bucket_cir;
    ds_grp_shp_wfq_ctl.que_offset7_sel_cir  = shp_bucket_cir;

    ds_grp_shp_wfq_ctl.que_offset0_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset1_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset2_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset3_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset4_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset5_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset6_sel_pir0 = shp_bucket_pir;
    ds_grp_shp_wfq_ctl.que_offset7_sel_pir0 = shp_bucket_pir;

    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, p_queue_info->group_id, cmd, &ds_grp_shp_wfq_ctl), ret, error_1);

    CTC_ERROR_GOTO(_sys_greatbelt_queue_id_alloc(lchip, queue_num, &queue_base), ret, error_1);

    p_queue_info->queue_type = queue_type;
    p_queue_info->queue_base = queue_base;
    p_queue_info->queue_num  = queue_num;

    CTC_ERROR_GOTO(_sys_greatbelt_queue_add(lchip, p_queue_info), ret, error_1);

    /*recovery ther service_id*/
    p_queue_info->service_id = service_id;
    p_queue_info->ref_cnt++;
    sys_greatbelt_queue_info_add(lchip, p_queue_info);

    return CTC_E_NONE;

error_1:
    mem_free(p_queue_info);
    return ret;

}

int32 sys_greatbelt_excp_queue_init(uint8 lchip,
                                    uint16 queue_base,
                                    uint8  channel,
                                    uint8 group_id)
{
       uint8 offset = 0;
       uint16 queue_id = 0;

       for (offset = 0; offset < p_gb_queue_master[lchip]->cpu_reason_grp_que_num; offset++)
       {

           queue_id = queue_base + offset;
           CTC_ERROR_RETURN(sys_greatbelt_queue_add_to_channel(lchip, queue_id, channel));
           CTC_ERROR_RETURN(sys_greatbelt_queue_add_to_group(lchip, queue_id, group_id));
            /*CTC_ERROR_RETURN(sys_greatbelt_group_add_to_channel(lchip, group_id, channel));*/
       }

    return CTC_E_NONE;

}

int32
sys_greatbelt_queue_c2c_init(uint8 lchip, uint8 c2c_group)
{
    ds_queue_hash_key_t queue_key;
    ds_queue_num_gen_ctl_t ds_queue_num_gen_ctl;
    sys_queue_group_node_t* p_sys_group_node = NULL;
    uint8 group_id = 0;
    uint8 index[80]  = {0};
    uint32 cmd        = 0;
    uint8 qsel_shift  = 0;
    uint8 idx         = 0;
    uint8 i           = 0;
    uint8 queue_num = 16;

    sal_memset(&queue_key, 0 , sizeof(ds_queue_hash_key_t));
    sal_memset(&ds_queue_num_gen_ctl, 0 , sizeof(ds_queue_num_gen_ctl_t));

    group_id = c2c_group + p_gb_queue_master[lchip]->reason_grp_start;

     p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
     if (NULL == p_sys_group_node)
     {
         return CTC_E_EXIST;
     }

    queue_key.hash_type0    = 1;
    queue_key.hash_type1    = queue_key.hash_type0;
    /*SYS_QSEL_TYPE_C2C << 13 mean c2c pkt to cpu*/
    queue_key.dest_queue0 = SYS_QSEL_TYPE_C2C << 13;
    queue_key.queue_base0 = p_sys_group_node->start_queue_id;
    queue_key.valid0        = 1;

    ds_queue_num_gen_ctl.dest_queue_en      = 1;
    ds_queue_num_gen_ctl.dest_queue_mask    = 0xFF60;

    /*from uplink to local (ucast)*/
    index[idx++] = SYS_QNUM_GEN_CTL_INDEX(1, 0, 0, 1, SYS_QSEL_TYPE_C2C);

    /*write key*/
    SYS_QUEUE_DBG_INFO("queue_key.hash_type0:%d\n",  queue_key.hash_type0);
    SYS_QUEUE_DBG_INFO("queue_key.hash_type1:%d\n",  queue_key.hash_type1);
    SYS_QUEUE_DBG_INFO("queue_key.dest_queue0:%d\n",  queue_key.dest_queue0);
    SYS_QUEUE_DBG_INFO("queue_key.queue_base0:%d\n",  queue_key.queue_base0);
    SYS_QUEUE_DBG_INFO("queue_key.valid0:%d\n",       queue_key.valid0);
    CTC_ERROR_RETURN(_sys_greatbelt_queue_remove_queue_key(lchip, &queue_key));
    CTC_ERROR_RETURN(_sys_greatbelt_queue_add_queue_key(lchip, &queue_key));

    /*write queue number gen ctl*/
    _sys_greatbelt_queue_get_qsel_shift(lchip, queue_num, &qsel_shift);
    SYS_QUEUE_DBG_INFO("queue_num:%d,  qsel_shift:%d\n", queue_num, qsel_shift);

    ds_queue_num_gen_ctl.queue_select_shift = qsel_shift;
    ds_queue_num_gen_ctl.queue_gen_mode     = 1;
    ds_queue_num_gen_ctl.flow_id_mask       = 0;
    cmd = DRV_IOW(DsQueueNumGenCtl_t, DRV_ENTRY_FLAG);

    for (i = 0; i < idx; i++)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index[i], cmd, &ds_queue_num_gen_ctl));
    }

    CTC_ERROR_RETURN(sys_greatbelt_queue_sch_set_c2c_group_sched(lchip, group_id, 2));
    CTC_ERROR_RETURN(sys_greatbelt_queue_sch_set_c2c_group_sched(lchip, group_id + 1, 3));

    return CTC_E_NONE;
}



int32
sys_greatbelt_excp_queue_add(uint8 lchip,
                             uint16 reason_id,
                             uint8 offset,
                             uint8 group_id)
{
    uint16 queue_base = 0;
    uint8 queue_num = 0;
    uint8 queue_type = 0;
    uint16 queue_id = 0;
    uint8 channel = 0;
    uint8 cpu_port_en = 0;
    uint16 sub_queue_id = 0;
    sys_queue_reason_t* p_reason = NULL;
    sys_queue_info_t* p_queue_info = NULL;
    sys_queue_group_node_t* p_sys_group_node = NULL;

    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_DBG_INFO("reason_id = %d, offset = %d, group_id = %d\n",
                       reason_id, offset, group_id);

    CTC_MAX_VALUE_CHECK(reason_id, CTC_PKT_CPU_REASON_MAX_COUNT - 1);
    CTC_MAX_VALUE_CHECK(offset, 7);
    CTC_MAX_VALUE_CHECK(group_id, 15);
    if ((CTC_PKT_CPU_REASON_ARP_MISS == reason_id)
        &&sys_greatbelt_nh_get_arp_num(lchip))
    {
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /*c2c pkt need 2 group*/
    if (reason_id == CTC_PKT_CPU_REASON_C2C_PKT)
    {
        CTC_MAX_VALUE_CHECK(group_id, 14);
    }

    /*Because all reason need create when init, so don't need rollback*/
    p_reason = ctc_vector_get(p_gb_queue_master[lchip]->reason_vec, reason_id);
    if (NULL == p_reason)
    {
        p_reason = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_reason_t));
        if (NULL == p_reason)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_reason, 0, sizeof(sys_queue_reason_t));
        ctc_vector_add(p_gb_queue_master[lchip]->reason_vec, reason_id, p_reason);
    }

    p_queue_info = &p_reason->queue_info;

    if ((reason_id >= CTC_PKT_CPU_REASON_OAM
         && reason_id < (CTC_PKT_CPU_REASON_OAM + CTC_PKT_CPU_REASON_OAM_PDU_RESV)) ||
        (reason_id == CTC_PKT_CPU_REASON_FWD_CPU))
    {
        if (reason_id == CTC_PKT_CPU_REASON_FWD_CPU)
        {
            sal_memcpy(p_queue_info,
                       &p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU].queue_info,
                       sizeof(sys_queue_info_t));
        }
        else
        {
            sal_memcpy(p_queue_info,
               &p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU_OAM].queue_info,
               sizeof(sys_queue_info_t));
        }

        CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_dest(lchip,
                                                           reason_id,
                                                           CTC_PKT_CPU_REASON_TO_LOCAL_CPU,
                                                           0));
        return CTC_E_NONE;
    }

    queue_type = SYS_QUEUE_TYPE_EXCP;
    queue_num = p_gb_queue_master[lchip]->queue_num_ctl[queue_type].queue_num;
    group_id = group_id + p_gb_queue_master[lchip]->reason_grp_start;

    if (p_queue_info->queue_num)
    {
        SYS_QUEUE_DBG_INFO("p_queue_info->offset:%d, offset:%d\n", p_queue_info->offset, offset);
        SYS_QUEUE_DBG_INFO("p_queue_info->group_id:%d, group_id:%d\n", p_queue_info->offset, offset);
        if (p_queue_info->offset == offset && p_queue_info->group_id == group_id)
        {
            return CTC_E_NONE;
        }

        p_queue_info->offset = offset;
    }
    else
    {
        p_queue_info->queue_num = queue_num;
        p_queue_info->queue_type = queue_type;
        p_queue_info->offset = offset;
        p_queue_info->channel = SYS_CPU_CHANNEL_ID;
    }

    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        p_sys_group_node = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_group_node_t));
        if (NULL == p_sys_group_node)
        {
            return CTC_E_NO_MEMORY;
        }

        sal_memset(p_sys_group_node, 0, sizeof(sys_queue_group_node_t));
        p_sys_group_node->p_queue_list = ctc_slist_new();
        ctc_vector_add(p_gb_queue_master[lchip]->group_vec, group_id, p_sys_group_node);

        /* Alloc new queue */
        CTC_ERROR_RETURN(_sys_greatbelt_queue_id_alloc(lchip,
                                                       p_gb_queue_master[lchip]->cpu_reason_grp_que_num,
                                                       &queue_base));
        CTC_ERROR_RETURN(sys_greatbelt_chip_get_cpu_eth_en(lchip, &cpu_port_en));
        channel = cpu_port_en? SYS_CPU_CHANNEL_ID : SYS_DMA_CHANNEL_ID;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_init(lchip, queue_base, channel, group_id));

        queue_id = queue_base + offset;
    }
    else
    {
        queue_id = p_sys_group_node->start_queue_id + offset;
    }
    sub_queue_id = (group_id -p_gb_queue_master[lchip]->reason_grp_start) * p_gb_queue_master[lchip]->cpu_reason_grp_que_num +  offset;

    p_queue_info->queue_base = queue_id;

    SYS_QUEUE_DBG_INFO("p_queue_info->queue_base = %d \n",p_queue_info->queue_base);

    /*Api assing the queue_id*/
    p_queue_info->group_id = group_id;
    p_queue_info->dest_queue = SYS_REASON_ENCAP_DEST_ID(sub_queue_id);
    SYS_QUEUE_DBG_INFO("p_queue_info->dest_queue = %d \n", p_queue_info->dest_queue);

    CTC_ERROR_RETURN(_sys_greatbelt_queue_add(lchip, p_queue_info));

    if (reason_id == CTC_PKT_CPU_REASON_C2C_PKT)
    {
        p_gb_queue_master[lchip]->c2c_group_base = group_id - p_gb_queue_master[lchip]->reason_grp_start;
        CTC_ERROR_RETURN(sys_greatbelt_queue_c2c_init(lchip, p_gb_queue_master[lchip]->c2c_group_base));
    }

    if (CTC_PKT_CPU_REASON_MIRRORED_TO_CPU == reason_id)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_dest(lchip,
                                                       reason_id,
                                                       CTC_PKT_CPU_REASON_TO_LOCAL_CPU,
                                                       0));
    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_queue_num(uint8 lchip, ctc_qos_queue_number_t* p_queue_num)
{
    uint8 type = 0;
    uint8 lport = 0;
    sys_queue_info_t* p_queue_info = NULL;

    SYS_QUEUE_DBG_FUNC();

    switch (p_queue_num->queue_type)
    {
    case CTC_QUEUE_TYPE_NETWORK_EGRESS:     /* network egress queue */
    case CTC_QUEUE_TYPE_NORMAL_CPU:         /* normal cpu queue */
    case CTC_QUEUE_TYPE_OAM:                /* oam queue */
        type = SYS_QUEUE_TYPE_NORMAL;
        break;

    case CTC_QUEUE_TYPE_FABRIC:           /* packet to fabric */
        type = SYS_QUEUE_TYPE_STACKING;
        break;

    case CTC_QUEUE_TYPE_EXCP_CPU:           /* packet-to-CPU exception queue */
    default:
        return CTC_E_INVALID_PARAM;
    }
    p_gb_queue_master[lchip]->queue_num_ctl[type].queue_num = p_queue_num->queue_number;

    for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
    {
        p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;

        if (p_queue_info->queue_type == type)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_port_queue_add(lchip,
                                                          type,
                                                          lport,
                                                          0));
        }
    }

    return CTC_E_NONE;
}

/**
 @brief Mapping a queue in the specific local chip to the given channel.
*/
int32
sys_greatbelt_queue_add_to_channel(uint8 lchip,
                                   uint16 queue_id,
                                   uint8 channel)
{
    sys_queue_node_t* p_sys_queue_node = NULL;
    uint32 cmd = 0;
    ds_que_map_t ds_que_map;

    CTC_MAX_VALUE_CHECK(queue_id, SYS_MAX_QUEUE_NUM - 1);

    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_DBG_PARAM("lchip = %d, queue_id = %d, channel = %d\n",
                        lchip, queue_id, channel);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_QUEUE_DBG_INFO("p_sys_queue_node: queue_id = %d, channel = %d, group_id = %d\n",
                       p_sys_queue_node->queue_id,
                       p_sys_queue_node->channel,
                       p_sys_queue_node->group_id);

    if (p_sys_queue_node->channel == channel)
    {
        return CTC_E_NONE;
    }

    if (SYS_MAX_CHANNEL_NUM != p_sys_queue_node->channel)
    {
        CTC_ERROR_RETURN(sys_greatbelt_queue_remove_from_channel(lchip,
                                                                 queue_id,
                                                                 p_sys_queue_node->channel));
    }

    /* write to asic */
    sal_memset(&ds_que_map, 0, sizeof(ds_que_map_t));
    cmd = DRV_IOR(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));
    ds_que_map.chan_id = channel;
    ds_que_map.offset  = p_sys_queue_node->offset % 8;
    cmd = DRV_IOW(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));

    /* write to db */
    p_sys_queue_node->channel = channel;


    SYS_QUEUE_DBG_INFO("Add queue to channel, lchip = %d, queue_id = %d, channel = %d\n", lchip, queue_id, channel);

    return CTC_E_NONE;
}

/**
 @brief Remove a queue in the specific local chip from the given channel.
*/
int32
sys_greatbelt_queue_remove_from_channel(uint8 lchip,
                                        uint16 queue_id,
                                        uint8 channel)
{
    sys_queue_node_t* p_sys_queue_node;
    uint32 cmd = 0;
    ds_que_map_t ds_que_map;

    CTC_MAX_VALUE_CHECK(queue_id, SYS_MAX_QUEUE_NUM - 1);

    SYS_QUEUE_DBG_FUNC();

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }


    channel = p_sys_queue_node->channel;

    if (SYS_MAX_CHANNEL_NUM == channel)
    {
        return CTC_E_NONE;
    }

    /* write to asic */
    sal_memset(&ds_que_map, 0, sizeof(ds_que_map_t));
    cmd = DRV_IOR(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));
    ds_que_map.chan_id = 63;/*select 63 as drop channel id, actually use shaping drop*/
    ds_que_map.offset = p_sys_queue_node->offset;
    cmd = DRV_IOW(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));

    /* write to db */

    p_sys_queue_node->channel = SYS_MAX_CHANNEL_NUM;

    SYS_QUEUE_DBG_INFO("Remove queue from channel, lchip = %d, queue_id = %d, channel = %d\n", lchip, queue_id, channel);

    return CTC_E_NONE;
}

int32
sys_greatbelt_add_port_to_channel(uint8 lchip, uint16 gport, uint8 channel)
{
    uint8 lport = 0;
    uint8 queue_num = 0;
    uint16 queue_base = 0;
    uint8 offset = 0;
    uint16 queue_id = 0;
    sys_queue_info_t* p_queue_info = NULL;

    SYS_QUEUE_DBG_FUNC();

    SYS_QUEUE_DBG_INFO("Add port(0x%x) to channel(%d) \n", gport, channel);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    SYS_QOS_QUEUE_LOCK(lchip);
    p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;

    queue_num = p_queue_info->queue_num;
    queue_base = p_queue_info->queue_base;

    /*write queue chan mapping*/
    for (offset = 0; offset < queue_num; offset++)
    {
        queue_id = queue_base + offset;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_queue_add_to_channel(lchip, queue_id, channel));
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_greatbelt_remove_port_from_channel(uint8 lchip, uint16 gport, uint8 channel)
{
    uint8 lport = 0;
    uint8 queue_num = 0;
    uint16 queue_base = 0;
    uint8 offset = 0;
    uint16 queue_id = 0;
    sys_queue_info_t* p_queue_info = NULL;

    SYS_QUEUE_DBG_FUNC();

    SYS_QUEUE_DBG_INFO("Remove port(0x%x) from channel(%d) \n", gport, channel);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    SYS_QOS_QUEUE_LOCK(lchip);
    p_queue_info = &p_gb_queue_master[lchip]->channel[lport].queue_info;
    queue_num = p_queue_info->queue_num;
    queue_base = p_queue_info->queue_base;

    /*write queue chan mapping*/
    for (offset = 0; offset < queue_num; offset++)
    {
        queue_id = queue_base + offset;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_queue_remove_from_channel(lchip, queue_id, channel));
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
 @brief Mapping a queue in the specific local chip to the given channel.
*/
int32
sys_greatbelt_queue_add_to_group(uint8 lchip,
                                 uint16 queue_id,
                                 uint16 group_id)
{
    sys_queue_node_t* p_sys_queue_node = NULL;
    sys_queue_group_node_t* p_sys_group_node = NULL;

    uint32 cmd = 0;
    ds_que_map_t ds_que_map;
    ds_grp_shp_wfq_ctl_t ds_grp_shp_wfq_ctl;

    CTC_MAX_VALUE_CHECK(queue_id, SYS_MAX_QUEUE_NUM - 1);

    SYS_QUEUE_DBG_FUNC();

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (p_sys_queue_node->group_id == group_id)
    {
        return CTC_E_NONE;
    }

    if (SYS_MAX_QUEUE_GROUP_NUM != p_sys_queue_node->group_id)
    {
        CTC_ERROR_RETURN(sys_greatbelt_queue_remove_from_group(lchip,
                                                               queue_id,
                                                               p_sys_queue_node->group_id));
    }


    /* write to db */
    p_sys_queue_node->group_id = group_id;
    p_sys_group_node->start_queue_id = queue_id - (p_sys_queue_node->offset % 8);

    /* write to asic */
    sal_memset(&ds_que_map, 0, sizeof(ds_que_map_t));
    cmd = DRV_IOR(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));
    ds_que_map.grp_id = group_id;
    cmd = DRV_IOW(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));

    sal_memset(&ds_grp_shp_wfq_ctl, 0, sizeof(ds_grp_shp_wfq_ctl_t));
    cmd = DRV_IOR(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));
    ds_grp_shp_wfq_ctl.start_que_id = p_sys_group_node->start_queue_id;
    ds_grp_shp_wfq_ctl.grp_bucket_bonding = p_sys_group_node->group_bond_en;
    cmd = DRV_IOW(DsGrpShpWfqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, group_id, cmd, &ds_grp_shp_wfq_ctl));

    ctc_slist_add_tail(p_sys_group_node->p_queue_list,
                       &p_sys_queue_node->head);

    SYS_QUEUE_DBG_INFO("Add queue to group, lchip = %d, queue_id = %d, group_id = %d\n",
                       lchip, queue_id, group_id);

    return CTC_E_NONE;
}

/**
 @brief Remove a queue in the specific local chip from the given channel.
*/
int32
sys_greatbelt_queue_remove_from_group(uint8 lchip,
                                      uint16 queue_id,
                                      uint16 group_id)
{
    sys_queue_node_t* p_sys_queue_node = NULL;
    sys_queue_group_node_t* p_sys_group_node = NULL;
    uint32 cmd = 0;
    ds_que_map_t ds_que_map;

    CTC_MAX_VALUE_CHECK(queue_id, SYS_MAX_QUEUE_NUM - 1);

    SYS_QUEUE_DBG_FUNC();

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    if (NULL == p_sys_queue_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
    if (NULL == p_sys_group_node)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (p_sys_queue_node->group_id != group_id)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* write to asic */
    sal_memset(&ds_que_map, 0, sizeof(ds_que_map_t));
    cmd = DRV_IOR(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));
    ds_que_map.grp_id = 0; /*!!!!!!!!!!!!!!!!!need considering!!!!!!!!!!!!*/
    cmd = DRV_IOW(DsQueMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_que_map));

    /* write to db */
    ctc_slist_delete_node(p_sys_group_node->p_queue_list,
                          &p_sys_queue_node->head);

    SYS_QUEUE_DBG_INFO("Remove queue from group, lchip = %d, queue_id = %d, group_id = %d\n",
                       lchip, queue_id, group_id);

    return CTC_E_NONE;
}

/**
 @brief set priority color map to queue select and drop_precedence.
*/
int32
sys_greatbelt_set_priority_queue_map(uint8 lchip, ctc_qos_pri_map_t* p_queue_pri_map)
{
    uint32 index = 0;
    uint32 cmd = 0;

    ds_queue_select_map_t ds_queue_select_map;
     /*ds_igr_pri_to_tc_map_t ds_igr_pri_to_tc_map;*/
    CTC_MAX_VALUE_CHECK(p_queue_pri_map->priority, SYS_QOS_CLASS_PRIORITY_MAX);
    CTC_MAX_VALUE_CHECK(p_queue_pri_map->queue_select, SYS_QOS_CLASS_PRIORITY_MAX);
    sal_memset(&ds_queue_select_map, 0, sizeof(ds_queue_select_map_t));
     /*sal_memset(&ds_igr_pri_to_tc_map, 0, sizeof(ds_igr_pri_to_tc_map_t));*/

    index = (p_queue_pri_map->priority << 2) | p_queue_pri_map->color;

    cmd = DRV_IOR(DsQueueSelectMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_queue_select_map));

    ds_queue_select_map.drop_precedence = p_queue_pri_map->drop_precedence;
    ds_queue_select_map.queue_select = p_queue_pri_map->queue_select;
    cmd = DRV_IOW(DsQueueSelectMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_queue_select_map));

    /*
    cmd = DRV_IOR(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
    ds_igr_pri_to_tc_map.sc0 = 0;
    ds_igr_pri_to_tc_map.tc0 = 0;
    ds_igr_pri_to_tc_map.sc1 = 0;
    ds_igr_pri_to_tc_map.tc1 = p_queue_pri_map->queue_select / 8;
    cmd = DRV_IOW(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
    */

    return CTC_E_NONE;
}

int32
_sys_greatbelt_queue_select_init(uint8 lchip)
{
    uint8 priority = 0;
    uint8 color = 0;
    ctc_qos_pri_map_t qos_pri_map;

    sal_memset(&qos_pri_map, 0, sizeof(ctc_qos_pri_map_t));

    for (priority = 0; priority <= SYS_QOS_CLASS_PRIORITY_MAX; priority++)
    {
        for (color = 0; color < 4; color++)
        {
            qos_pri_map.priority = priority;
            qos_pri_map.color = color;
            qos_pri_map.queue_select = priority;
            qos_pri_map.drop_precedence = (color + 3) % 4;

            CTC_ERROR_RETURN(sys_greatbelt_set_priority_queue_map(lchip, &qos_pri_map));
        }
    }

    return CTC_E_NONE;

}

/**
 @brief reserved channel drop init
*/
int32
_sys_greatbelt_rsv_channel_drop_init(uint8 lchip)
{
    uint32 cmd = 0;
    uint8 drop_chan = 0xFF;
    q_mgr_reserved_channel_range_t  qmgt_rsv_chan_rag;

    sal_memset(&qmgt_rsv_chan_rag, 0, sizeof(q_mgr_reserved_channel_range_t));

    drop_chan =  SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DROP);
    if (0xFF != drop_chan)
    {
        /*rsv channel valid0 set*/
        qmgt_rsv_chan_rag.reserved_channel_valid0 = 1;
        qmgt_rsv_chan_rag.reserved_channel_min0 = drop_chan;
        qmgt_rsv_chan_rag.reserved_channel_max0 = drop_chan;
    }

    cmd = DRV_IOW(QMgrReservedChannelRange_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qmgt_rsv_chan_rag));

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_set_stats_enable(uint8 lchip, ctc_qos_queue_stats_t* p_stats)
{
    uint8  lport = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;
    uint16 queue_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;

    SYS_QUEUE_DBG_FUNC();

    if ((p_stats->queue.queue_type == CTC_QUEUE_TYPE_NETWORK_EGRESS)
        || (p_stats->queue.queue_type == CTC_QUEUE_TYPE_SERVICE_INGRESS))
    {
        SYS_MAP_GPORT_TO_LPORT(p_stats->queue.gport, lchip, lport);
    }

    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_stats->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("Queue id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    if (!p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_sys_queue_node->stats_en = p_stats->stats_en;
    SYS_QUEUE_DBG_INFO("p_sys_queue_node->stats_en :%d\n", p_sys_queue_node->stats_en );

    field_val = p_stats->stats_en;

    cmd = DRV_IOW(DsEgrResrcCtl_t, DsEgrResrcCtl_StatsUpdEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &field_val));

    return CTC_E_NONE;
}


int32
 sys_greatbelt_qos_bind_service_logic_port(uint8 lchip, uint16 service_id,
                                                uint16 logic_port)
 {
     sys_service_node_t* p_service = NULL;
     sys_service_node_t service;
     sys_qos_logicport_t* p_logic_port_node = NULL;
     sys_qos_destport_t* p_dest_port_node = NULL;
     ctc_slistnode_t* ctc_slistnode = NULL;
     bool b_found = FALSE;


    SYS_QUEUE_DBG_FUNC();
    SYS_QOS_QUEUE_LOCK(lchip);

     sal_memset(&service, 0, sizeof(service));
     service.service_id = service_id;
     p_service = sys_greatbelt_service_lookup(lchip, &service);
     if (NULL == p_service)
     {
         SYS_QOS_QUEUE_UNLOCK(lchip);
         return CTC_E_ENTRY_NOT_EXIST;
     }

     CTC_SLIST_LOOP(p_service->p_logic_port_list, ctc_slistnode)
     {
         p_logic_port_node = _ctc_container_of(ctc_slistnode, sys_qos_logicport_t, head);
         if (p_logic_port_node->logic_port == logic_port)
         {
             b_found = TRUE;
             break;
         }
     }

     if (b_found)
     {   /*logic port exist!!, not need bind*/
         SYS_QOS_QUEUE_UNLOCK(lchip);
         return CTC_E_NONE;
     }

     /*bind new logic port */
     p_logic_port_node = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_logicport_t));
     if (NULL == p_logic_port_node)
     {
         SYS_QOS_QUEUE_UNLOCK(lchip);
         return CTC_E_NO_MEMORY;
     }

     p_logic_port_node->logic_port = logic_port;
     ctc_slist_add_tail(p_service->p_logic_port_list, &p_logic_port_node->head);

     /*syn up queue alloc*/
     CTC_SLIST_LOOP(p_service->p_dest_port_list, ctc_slistnode)
     {
         p_dest_port_node = _ctc_container_of(ctc_slistnode, sys_qos_destport_t, head);
         sys_greatbelt_service_queue_add(p_dest_port_node->lchip,
                                         service_id,
                                         logic_port,
                                         p_dest_port_node->lport);
     }
     SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
 }

int32
sys_greatbelt_qos_unbind_service_logic_port(uint8 lchip, uint16 service_id,
                                                uint16 logic_port)
 {
     sys_service_node_t* p_service = NULL;
     sys_service_node_t service;
     sys_qos_logicport_t* p_logic_port_node = NULL;
     sys_qos_destport_t* p_dest_port_node = NULL;
     ctc_slistnode_t* ctc_slistnode = NULL;
     bool b_found = FALSE;


    SYS_QUEUE_DBG_FUNC();

     SYS_QOS_QUEUE_LOCK(lchip);
     sal_memset(&service, 0, sizeof(service));
     service.service_id = service_id;
     p_service = sys_greatbelt_service_lookup(lchip, &service);
     if (NULL == p_service)
     {
         SYS_QOS_QUEUE_UNLOCK(lchip);
         return CTC_E_ENTRY_NOT_EXIST;
     }

     CTC_SLIST_LOOP(p_service->p_logic_port_list, ctc_slistnode)
     {
         p_logic_port_node = _ctc_container_of(ctc_slistnode, sys_qos_logicport_t, head);
         if (p_logic_port_node->logic_port == logic_port)
         {
             b_found = TRUE;
             break;
         }
     }
     if (!b_found)
     {   /*logic port exist!!, not need bind*/
         SYS_QOS_QUEUE_UNLOCK(lchip);
         return CTC_E_NONE;
     }

    /*unbind logic port */
    CTC_SLIST_LOOP(p_service->p_dest_port_list, ctc_slistnode)
    {
        p_dest_port_node = _ctc_container_of(ctc_slistnode, sys_qos_destport_t, head);
        sys_greatbelt_service_queue_remove(p_dest_port_node->lchip,
                                         service_id,
                                         logic_port ,
                                         p_dest_port_node->lport);
    }


    ctc_slist_delete_node(p_service->p_logic_port_list, &p_logic_port_node->head);
    mem_free(p_logic_port_node);

    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
 }


uint32
sys_greatbelt_qos_bind_service_dstport(uint8 lchip, uint32 service_id, uint16 dest_port)
{
    sys_service_node_t* p_service = NULL;
    sys_service_node_t service;
    sys_qos_destport_t* p_dest_port_node = NULL;
    sys_qos_logicport_t* p_logic_port_node = NULL;
    uint16 logic_port = 0;
    uint8 lport = 0;
    ctc_slistnode_t* ctc_slistnode = NULL;
    bool b_found = FALSE;


    SYS_QUEUE_DBG_FUNC();

    sal_memset(&service, 0, sizeof(service));
    service.service_id = service_id;
    p_service = sys_greatbelt_service_lookup(lchip, &service);
    if (NULL == p_service)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_MAP_GPORT_TO_LPORT(dest_port, lchip, lport);

    CTC_SLIST_LOOP(p_service->p_dest_port_list, ctc_slistnode)
    {
        p_dest_port_node = _ctc_container_of(ctc_slistnode, sys_qos_destport_t, head);
        if (p_dest_port_node->lchip == lchip &&
            p_dest_port_node->lport == lport)
        {
            b_found = TRUE;
            break;
        }
    }

    if (b_found)
    {
        return CTC_E_NONE;
    }


     /*bind new logic port */
     p_dest_port_node = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_qos_destport_t));
     if (NULL == p_dest_port_node)
     {
         return CTC_E_NO_MEMORY;
     }

     p_dest_port_node->lchip = lchip;
     p_dest_port_node->lport = lport;
     ctc_slist_add_tail(p_service->p_dest_port_list, &p_dest_port_node->head);


    CTC_SLIST_LOOP(p_service->p_logic_port_list, ctc_slistnode)
    {
        p_logic_port_node = _ctc_container_of(ctc_slistnode, sys_qos_logicport_t, head);
        logic_port = p_logic_port_node->logic_port;

        CTC_ERROR_RETURN(sys_greatbelt_service_queue_add(lchip,
                                                         service_id,
                                                         logic_port,
                                                         lport));
     }

    return CTC_E_NONE;
}


uint32
sys_greatbelt_qos_unbind_service_dstport(uint8 lchip, uint32 service_id, uint16 dest_port)
{
    sys_service_node_t* p_service = NULL;
    sys_service_node_t service;
    sys_qos_destport_t* p_dest_port_node = NULL;
    sys_qos_logicport_t* p_logic_port_node = NULL;
    uint16 logic_port = 0;
    uint8 lport = 0;
    ctc_slistnode_t* ctc_slistnode = NULL;
    bool b_found = FALSE;

    SYS_QUEUE_DBG_FUNC();

    sal_memset(&service, 0, sizeof(service));
    service.service_id = service_id;
    p_service = sys_greatbelt_service_lookup(lchip, &service);
    if (NULL == p_service)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_MAP_GPORT_TO_LPORT(dest_port, lchip, lport);

    CTC_SLIST_LOOP(p_service->p_dest_port_list, ctc_slistnode)
    {
        p_dest_port_node = _ctc_container_of(ctc_slistnode, sys_qos_destport_t, head);
        if (p_dest_port_node->lchip == lchip &&
            p_dest_port_node->lport == lport)
        {
            b_found = TRUE;
            break;
        }
    }

    if (!b_found)
    {
        return CTC_E_NONE;
    }

    CTC_SLIST_LOOP(p_service->p_logic_port_list, ctc_slistnode)
    {
        p_logic_port_node = _ctc_container_of(ctc_slistnode, sys_qos_logicport_t, head);
        logic_port = p_logic_port_node->logic_port;
        CTC_ERROR_RETURN(sys_greatbelt_service_queue_remove(lchip,
                                                            service_id,
                                                            logic_port,
                                                            lport));
    }


    ctc_slist_delete_node(p_service->p_dest_port_list, &p_dest_port_node->head);
    mem_free(p_dest_port_node);


    return CTC_E_NONE;
}




int32
sys_greatbelt_queue_set_queue_pkt_adjust(uint8 lchip, ctc_qos_queue_pkt_adjust_t* p_pkt)
{

    sys_queue_node_t* p_sys_queue_node = NULL;
    uint16 queue_id = 0;
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 index = 0;
    uint8 pkt_index = 0;
    uint8 exist = 0;

    SYS_QUEUE_DBG_FUNC();


    CTC_ERROR_RETURN(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_pkt->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("Queue id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    if (!p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    for (index = 0; index < 7; index++)
    {
        if (p_gb_queue_master[lchip]->queue_pktadj_len[index] == 0)
        {
            pkt_index = index;
        }

        if (p_gb_queue_master[lchip]->queue_pktadj_len[index] == p_pkt->adjust_len)
        {
            pkt_index = index;
            exist = 1;
            break;
        }
    }

    field_val = pkt_index + 1;
    cmd = DRV_IOW(DsEgrResrcCtl_t, DsEgrResrcCtl_NetPktAdjIndex_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &field_val));

    if (!exist)
    {
        field_val = p_pkt->adjust_len;
        cmd = DRV_IOW(QMgrNetPktAdj_t, QMgrNetPktAdj_NetPktAdjVal0_f +  pkt_index + 1);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_gb_queue_master[lchip]->queue_pktadj_len[pkt_index] = p_pkt->adjust_len;
    }

     SYS_QUEUE_DBG_INFO("Queue id = %d, NetPktAdjIndex:%d, value:%d\n", queue_id,
          pkt_index + 1, p_pkt->adjust_len);


    return CTC_E_NONE;
}

int32
_sys_greatbelt_qos_service_proc(uint8 lchip, ctc_qos_service_info_t *p_service_para)
{
    uint32 service_id = p_service_para->service_id;

    SYS_QUEUE_DBG_FUNC();
    switch(p_service_para->opcode)
    {
    case CTC_QOS_SERVICE_ID_CREATE:
        CTC_ERROR_RETURN(
        sys_greatbelt_qos_create_service(lchip, service_id));
        break;

    case CTC_QOS_SERVICE_ID_DESTROY:
        CTC_ERROR_RETURN(
        sys_greatbelt_qos_destroy_service(lchip, service_id));
        break;

    case CTC_QOS_SERVICE_ID_BIND_DESTPORT:
        CTC_ERROR_RETURN(
        sys_greatbelt_qos_bind_service_dstport(lchip, service_id, p_service_para->gport));
        break;

    case CTC_QOS_SERVICE_ID_UNBIND_DESTPORT:
        CTC_ERROR_RETURN(
        sys_greatbelt_qos_unbind_service_dstport(lchip, service_id, p_service_para->gport));
        break;

    default:
        return CTC_E_INTR_INVALID_PARAM;
    }
    return CTC_E_NONE;
}


int32
sys_greatbelt_cpu_reason_set_mode(uint8 lchip, uint8 group_type, uint8 reason_group, uint8 mode)
{
    uint16 group_id = 0 ;
    sys_queue_group_node_t* p_sys_group_node = NULL;
    ctc_slist_t* p_queue_list = NULL;
    sys_queue_node_t* p_queue_node = NULL;
    ctc_slistnode_t* ctc_slistnode = NULL;
    uint16 queue_id = 0;
    uint8 channel = 0;


    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_DBG_PARAM("group_type = %d, reason_group :%d, mode:%d\n", group_type, reason_group, mode);

    if (CTC_PKT_CPU_REASON_GROUP_FORWARD == group_type)
    {
        group_id = p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU].queue_info.group_id;
    }
    else if (CTC_PKT_CPU_REASON_GROUP_OAM == group_type)
    {
        group_id = p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU_OAM].queue_info.group_id;
    }
    else if(CTC_PKT_CPU_REASON_GROUP_REMOTE_FORWARD == group_type)
    {
        group_id = p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU_REMOTE].queue_info.group_id;
    }
    else
    {
        group_id = reason_group + p_gb_queue_master[lchip]->reason_grp_start;
    }

    if (CTC_PKT_MODE_DMA == mode)
    {
        channel = SYS_DMA_CHANNEL_ID;
    }
    else
    {
        channel = SYS_CPU_CHANNEL_ID;
    }


    p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);

    if (NULL == p_sys_group_node)
    {
        return CTC_E_NONE;
    }

    p_queue_list = p_sys_group_node->p_queue_list;

    CTC_SLIST_LOOP(p_queue_list, ctc_slistnode)
    {
        p_queue_node = _ctc_container_of(ctc_slistnode, sys_queue_node_t, head);
        if (!p_queue_node)
        {
            continue;
        }

        queue_id = p_queue_node->queue_id;
        sys_greatbelt_queue_add_to_channel(lchip, queue_id, channel);
    }

    return CTC_E_NONE;

}

STATIC int32
_sys_greatbelt_cpu_reason_mode_init(uint8 lchip)
{
    uint8 cpu_port_en = 0;
    uint8 mode = CTC_PKT_MODE_DMA;
    uint8 group_type = 0;
    uint8 group_id = 0;

    CTC_ERROR_RETURN(sys_greatbelt_chip_get_cpu_eth_en(lchip, &cpu_port_en));
    mode = cpu_port_en? CTC_PKT_MODE_ETH : CTC_PKT_MODE_DMA;

    SYS_QUEUE_DBG_DUMP("Packet To CPU by %s!\n", cpu_port_en? "ETH" : "DMA");

    group_type = CTC_PKT_CPU_REASON_GROUP_OAM;
    CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_mode(lchip, group_type, group_id, mode));

    group_type = CTC_PKT_CPU_REASON_GROUP_FORWARD;
    CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_mode(lchip, group_type, group_id, mode));

    group_type = CTC_PKT_CPU_REASON_GROUP_REMOTE_FORWARD;
    CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_mode(lchip, group_type, group_id, mode));

    group_type = CTC_PKT_CPU_REASON_GROUP_EXCEPTION;
    for (group_id = 0; group_id < SYS_QUEUE_MAX_GRP_NUM_FOR_CPU_REASON; group_id++)
    {
        CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_mode(lchip, group_type, group_id, mode));
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_queue_set(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg)
{

    SYS_QUEUE_DBG_FUNC();
    SYS_QUEUE_INIT_CHECK(lchip);
    SYS_QOS_QUEUE_LOCK(lchip);

    switch (p_que_cfg->type)
    {
    case CTC_QOS_QUEUE_CFG_PRI_MAP:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_set_priority_queue_map(lchip, &p_que_cfg->value.pri_map));
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_NUM:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_set_queue_num(lchip, &p_que_cfg->value.queue_num));
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_STATS_EN:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_set_stats_enable(lchip, &p_que_cfg->value.stats));
        break;


    case CTC_QOS_QUEUE_CFG_LENGTH_ADJUST:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
            sys_greatbelt_queue_set_queue_pkt_adjust(lchip, &p_que_cfg->value.pkt));
        break;


    case CTC_QOS_QUEUE_CFG_SCHED_WDRR_MTU:
        CTC_VALUE_RANGE_CHECK_QOS_QUEUE_UNLOCK(p_que_cfg->value.value_32, 1, 0xFFFF);
        p_gb_queue_master[lchip]->wrr_weight_mtu = p_que_cfg->value.value_32;
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_MAP:
        {
            ctc_qos_queue_cpu_reason_map_t* p_reason_map = &p_que_cfg->value.reason_map;
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_excp_queue_add(lchip,
                                                          p_reason_map->cpu_reason,
                                                          p_reason_map->queue_id,
                                                          p_reason_map->reason_group));

        }
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_DEST:
        {
            ctc_qos_queue_cpu_reason_dest_t* p_reason_dest = &p_que_cfg->value.reason_dest;
            if (CTC_PKT_CPU_REASON_TO_NHID == p_reason_dest->dest_type)
            {
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_cpu_reason_set_dest(lchip,
                                                                   p_reason_dest->cpu_reason,
                                                                   p_reason_dest->dest_type,
                                                                   p_reason_dest->nhid));
            }
            else
            {
                CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_cpu_reason_set_dest(lchip,
                                                                   p_reason_dest->cpu_reason,
                                                                   p_reason_dest->dest_type,
                                                                   p_reason_dest->dest_port));
            }

        }
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_PRIORITY:
        {
           ctc_qos_queue_cpu_reason_priority_t* p_reason_pri = &p_que_cfg->value.reason_pri;

            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_cpu_reason_set_priority(lchip,
                                                               p_reason_pri->cpu_reason,
                                                               p_reason_pri->cos));


        }
        break;

    case CTC_QOS_QUEUE_CFG_QUEUE_REASON_MODE:
        {
            ctc_qos_group_cpu_reason_mode_t* p_reason_mode = &p_que_cfg->value.reason_mode;
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(sys_greatbelt_cpu_reason_set_mode(lchip, p_reason_mode->group_type,
                                                               p_reason_mode->group_id,
                                                               p_reason_mode->mode));

        }
        break;



    case CTC_QOS_QUEUE_CFG_SERVICE_BIND:
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(
        _sys_greatbelt_qos_service_proc(lchip, &p_que_cfg->value.srv_queue_info));
        break;
    default:
        SYS_QOS_QUEUE_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_queue_get(uint8 lchip, ctc_qos_queue_cfg_t* p_que_cfg)
{
    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_resrc_mgr_en(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

     /*field_val = enable ? 0 : 1;*/

     /* cmd = DRV_IOW(BufStoreMiscCtl_t, BufStoreMiscCtl_ResrcMgrDisable_f);*/
     /* CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));*/

    /*configure discard port check condition profile*/
    field_val = enable ? 0 : 2;
    cmd = DRV_IOW(IgrCondDisProfile_t, IgrCondDisProfile_CondDisBmp0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));



    return CTC_E_NONE;
}

extern int32
sys_greatbelt_qos_resrc_mgr_init(uint8 lchip, ctc_qos_global_cfg_t* p_cfg)
{
    uint32 cmd = 0;
    uint8 chan_id = 0;
    uint8 lport = 0;
    uint8 priority = 0;
    uint8 color = 0;
    uint8 index = 0;
    uint8 tc = 0;
    uint32 field_id = 0;
    uint32 field_val = 0;

    buf_store_net_pfc_req_ctl_t buf_store_net_pfc_req_ctl;
    buffer_store_ctl_t buffer_store_ctl;
    buf_store_misc_ctl_t buf_store_misc_ctl;
    igr_resrc_mgr_misc_ctl_t igr_resrc_mgr_misc_ctl;
    buf_store_total_resrc_info_t buf_store_total_resrc_info;
    ds_igr_pri_to_tc_map_t ds_igr_pri_to_tc_map;
    igr_port_tc_min_profile_t igr_port_tc_min_profile;
    igr_tc_thrd_t igr_tc_thrd;
    igr_sc_thrd_t igr_sc_thrd;
    igr_congest_level_thrd_t igr_congest_level_thrd;
    igr_glb_drop_cond_ctl_t igr_glb_drop_cond_ctl;
    igr_cond_dis_profile_t igr_cond_dis_profile;

    uint32 pool_size[CTC_QOS_IGS_RESRC_POOL_MAX] = {0};

    /* 1. Buffer assign, total buffer cell is 12K */
    if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_DISABLE)
    {
        pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL]   = 10832;
        pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL]  = 1120;
        pool_size[CTC_QOS_IGS_RESRC_MIN_POOL]       = 0;
        pool_size[CTC_QOS_IGS_RESRC_C2C_POOL]       = 128;
        pool_size[CTC_QOS_IGS_RESRC_CONTROL_POOL]   = 128;
    }
    else if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_MODE1)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.igs_pool_size, sizeof(pool_size));
        if (pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL])
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_MODE2)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.igs_pool_size, sizeof(pool_size));
        if (pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL] == 0)
        {
            return CTC_E_INVALID_PARAM;
        }
    }
    else if (p_cfg->resrc_pool.igs_pool_mode == CTC_QOS_RESRC_POOL_USER_DEFINE)
    {
        sal_memcpy(pool_size, p_cfg->resrc_pool.igs_pool_size, sizeof(pool_size));
    }

    if (pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL] == 0)
    {
        return CTC_E_INVALID_PARAM;
    }


    /*
    (1) disable flow control for all ports
    (2) disable PFC for all ports and priorities
    */
    cmd = DRV_IOR(BufStoreNetPfcReqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_net_pfc_req_ctl));
    buf_store_net_pfc_req_ctl.pfc_chan_en_high = 0;
    buf_store_net_pfc_req_ctl.pfc_chan_en_low = 0;
    cmd = DRV_IOW(BufStoreNetPfcReqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_net_pfc_req_ctl));

    for (chan_id = 0; chan_id < SYS_MAX_CHANNEL_NUM; chan_id++)
    {
        field_id = BufStoreNetPfcReqCtl_PfcPriorityEnChan0_f + chan_id;
        field_val = 0;
        cmd = DRV_IOW(BufStoreNetPfcReqCtl_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    /*no ports have mux-demux ports*/
    sal_memset(&buffer_store_ctl, 0, sizeof(buffer_store_ctl_t));
    cmd = DRV_IOR(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));
    buffer_store_ctl.resrc_id_use_local_phy_port31_0 = 0;
    buffer_store_ctl.resrc_id_use_local_phy_port63_32 = 0;
    cmd = DRV_IOW(BufferStoreCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buffer_store_ctl));

    /* enable ingress resource management*/
    sal_memset(&buf_store_misc_ctl, 0, sizeof(buf_store_misc_ctl_t));
    cmd = DRV_IOR(BufStoreMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_misc_ctl));
    buf_store_misc_ctl.drain_enable = 1;
    buf_store_misc_ctl.resrc_mgr_disable = 0;
    buf_store_misc_ctl.hdr_crc_chk_enable = 1;
    buf_store_misc_ctl.over_len_error_chk_enable = 1;
    buf_store_misc_ctl.parity_en = 1;
    buf_store_misc_ctl.under_len_error_chk_enable = 1;
    buf_store_misc_ctl.max_pkt_size = 9216;
    buf_store_misc_ctl.min_pkt_size = 12;
    cmd = DRV_IOW(BufStoreMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_misc_ctl));

    /*critical: 128 cells, c2c: 128 cells*/
    sal_memset(&igr_resrc_mgr_misc_ctl, 0, sizeof(igr_resrc_mgr_misc_ctl_t));
    cmd = DRV_IOR(IgrResrcMgrMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_resrc_mgr_misc_ctl));
    /*configure c2c resource pool*/
    igr_resrc_mgr_misc_ctl.no_care_c2c_packet = 0;
    igr_resrc_mgr_misc_ctl.en_c2c_packet_chk = 0;
    igr_resrc_mgr_misc_ctl.c2c_packet_thrd = pool_size[CTC_QOS_IGS_RESRC_C2C_POOL];
    /*configure critical resource pool*/
    igr_resrc_mgr_misc_ctl.no_care_critical_packet = 0;
    igr_resrc_mgr_misc_ctl.en_critical_packet_chk = 0;
    igr_resrc_mgr_misc_ctl.critical_packet_thrd = pool_size[CTC_QOS_IGS_RESRC_CONTROL_POOL];
    cmd = DRV_IOW(IgrResrcMgrMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_resrc_mgr_misc_ctl));

    /*RT pool: 1120 cells*/
    sal_memset(&buf_store_total_resrc_info, 0, sizeof(buf_store_total_resrc_info_t));
    cmd = DRV_IOR(BufStoreTotalResrcInfo_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_total_resrc_info));
    /*Round-trip pool is reserved with 1120 cells*/
    buf_store_total_resrc_info.igr_total_thrd = (pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL])/ 16;
    cmd = DRV_IOW(BufStoreTotalResrcInfo_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &buf_store_total_resrc_info));

    /*
    two TC, SC
    mapping profiles
    only use profile 0: all priority mapped to tc priority/8 and sc 0
    */
    sal_memset(&ds_igr_pri_to_tc_map, 0, sizeof(ds_igr_pri_to_tc_map_t));

    for (priority = 0; priority < 64; priority++)
    {
        for (color = 0; color < 4; color++)
        {
            index = priority << 2 | color;
            cmd = DRV_IOR(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));
            ds_igr_pri_to_tc_map.sc0 = CTC_QOS_IGS_RESRC_DEFAULT_POOL;
            ds_igr_pri_to_tc_map.tc0 = priority / 8;
            ds_igr_pri_to_tc_map.sc1 = CTC_QOS_IGS_RESRC_DEFAULT_POOL;
            ds_igr_pri_to_tc_map.tc1 = priority / 8;
            cmd = DRV_IOW(DsIgrPriToTcMap_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &ds_igr_pri_to_tc_map));

        }
    }

    /*
     all port are mapping profile 0
    */
    for (chan_id = 0; chan_id < SYS_MAX_CHANNEL_NUM; chan_id++)
    {
        field_val = 0;
        field_id = DsIgrPortToTcProfId_ProfId0_f + chan_id % 8;
        index = chan_id / 8;
        cmd = DRV_IOW(DsIgrPortToTcProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

    }

    /*
    configure port, TC minimum guarantee profile
    profile 0: TcMin = 16
    profile 1: TcMin = 32
    */
    sal_memset(&igr_port_tc_min_profile, 0, sizeof(igr_port_tc_min_profile_t));
    cmd = DRV_IOR(IgrPortTcMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_port_tc_min_profile));
    igr_port_tc_min_profile.port_tc_min0 = 16;
    igr_port_tc_min_profile.port_tc_min1 = 32;
    cmd = DRV_IOW(IgrPortTcMinProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_port_tc_min_profile));

    /*
    all GE ports uses minimum guarantee profile 0: TcMin = 16
    all XGE ports uses minimum guarantee profile 1: TcMin = 32
    default all ports are 10G Port
    */
    for (chan_id = 0; chan_id < 64; chan_id++)
    {
        for (tc = 0; tc < 8; tc++)
        {
            field_val = 0;
            field_id = DsIgrPortTcMinProfId_ProfId0_f + tc;
            index = chan_id;
            cmd = DRV_IOW(DsIgrPortTcMinProfId_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        }
    }

    /*configure tc threshold, default disable*/
    sal_memset(&igr_tc_thrd, 0, sizeof(igr_tc_thrd_t));
    cmd = DRV_IOR(IgrTcThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_tc_thrd));
    igr_tc_thrd.tc_thrd0 = 9680 / 16;
    igr_tc_thrd.tc_thrd1 = 9808 / 16;
    igr_tc_thrd.tc_thrd2 = 9936 / 16;
    igr_tc_thrd.tc_thrd3 = 10064 / 16;
    igr_tc_thrd.tc_thrd4 = 10192 / 16;
    igr_tc_thrd.tc_thrd5 = 10320 / 16;
    igr_tc_thrd.tc_thrd6 = 10448 / 16;
    igr_tc_thrd.tc_thrd7 = 10576 / 16;

    cmd = DRV_IOW(IgrTcThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_tc_thrd));

    /*configure SC threshold*/
    sal_memset(&igr_sc_thrd, 0, sizeof(igr_sc_thrd_t));
    cmd = DRV_IOR(IgrScThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_sc_thrd));
    igr_sc_thrd.sc_thrd0 = pool_size[CTC_QOS_IGS_RESRC_DEFAULT_POOL] / 16;
    igr_sc_thrd.sc_thrd1 = pool_size[CTC_QOS_IGS_RESRC_NON_DROP_POOL] / 16;
    cmd = DRV_IOW(IgrScThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_sc_thrd));

    /*configure congestion level watermarks, disable ingress congest level*/
    sal_memset(&igr_congest_level_thrd, 0, sizeof(igr_congest_level_thrd_t));
    cmd = DRV_IOR(IgrCongestLevelThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_congest_level_thrd));
    igr_congest_level_thrd.sc0_thrd_lvl0 = 0xFF;
    igr_congest_level_thrd.sc0_thrd_lvl1 = 0xFF;
    igr_congest_level_thrd.sc0_thrd_lvl2 = 0xFF;
    igr_congest_level_thrd.sc0_thrd_lvl3 = 0xFF;
    igr_congest_level_thrd.sc0_thrd_lvl4 = 0xFF;
    igr_congest_level_thrd.sc0_thrd_lvl5 = 0xFF;
    igr_congest_level_thrd.sc0_thrd_lvl6 = 0xFF;
    cmd = DRV_IOW(IgrCongestLevelThrd_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_congest_level_thrd));

    /*configure discard check condition profile*/
    /*SYS_RESRC_IGS_COND(tc_min,total,sc,tc,port,port_tc)*/
    sal_memset(&igr_cond_dis_profile, 0, sizeof(igr_cond_dis_profile_t));
    cmd = DRV_IOR(IgrCondDisProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_cond_dis_profile));
    field_val = SYS_RESRC_IGS_COND(0, 0, 0, 1, 1, 1);
    igr_cond_dis_profile.cond_dis_bmp0 = field_val;
    field_val = SYS_RESRC_IGS_COND(0, 0, 0, 1, 0, 1);
    igr_cond_dis_profile.cond_dis_bmp1 = field_val;
    field_val = SYS_RESRC_IGS_COND(0, 0, 0, 1, 1, 0);
    igr_cond_dis_profile.cond_dis_bmp2 = field_val;
    cmd = DRV_IOW(IgrCondDisProfile_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_cond_dis_profile));

    for (lport = 0; lport < 64; lport++)
    {
        for (tc = 0; tc < 8; tc++)
        {
            field_val = 0;
            field_id = DsIgrCondDisProfId_ProfId0_f + tc;
            index = lport;
            cmd = DRV_IOW(DsIgrCondDisProfId_t, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        }
    }

    /*disable discard check for flow-ctrl and pfc on-flight packets*/
    sal_memset(&igr_glb_drop_cond_ctl, 0, sizeof(igr_glb_drop_cond_ctl_t));
    cmd = DRV_IOR(IgrGlbDropCondCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_glb_drop_cond_ctl));
    igr_glb_drop_cond_ctl.igr_glb_drop_cond_dis = 0x1F;
    igr_glb_drop_cond_ctl.igr_glb_tc_drop_cond_dis = 0x1F;
    cmd = DRV_IOW(IgrGlbDropCondCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &igr_glb_drop_cond_ctl));

    return CTC_E_NONE;
}


int32
sys_greatbelt_qos_flow_ctl_profile(uint8 lchip, sys_qos_fc_prop_t *fc_prop)
{
    uint32 cmd = 0;
    uint32 index = 0;
    uint32 field_id = 0;
    uint32 field_val = 0;
    uint8 lport = 0;
    uint8 channel = 0;
    uint32 pri = 0;
    drv_datapath_port_capability_t port_cap;

    SYS_MAP_GPORT_TO_LPORT(fc_prop->gport, lchip, lport);

    channel = SYS_GET_CHANNEL_ID(lchip, fc_prop->gport);
    if (0xFF ==channel)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    /* 1. read port Req status */
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

    if (!port_cap.valid)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }

    cmd = DRV_IOR(PauseReqTab_t, PauseReqTab_Pri_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd, &pri));

    /* 2. modify XON/XOFF threashold */
    if(fc_prop->is_pfc)
    {
        SYS_QOS_QUEUE_LOCK(lchip);
        field_val = fc_prop->enable ? p_gb_queue_master[lchip]->p_pfc_profile[channel][fc_prop->cos]->head.profile_id: 1;
        SYS_QOS_QUEUE_UNLOCK(lchip);
        SYS_QUEUE_DBG_INFO(" profile id = %d\n", field_val);
        field_id = DsIgrPortTcThrdProfId_ProfIdHigh0_f + (channel << 3 | fc_prop->cos) % 4;
        index = (channel << 3 | fc_prop->cos) / 4;
        cmd = DRV_IOW(DsIgrPortTcThrdProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        /*enable port tc drop check*/
        field_val = fc_prop->enable ? 2 : 0;  /* default use profile 0 */
        index = channel;
        cmd = DRV_IOW(DsIgrCondDisProfId_t, DsIgrCondDisProfId_ProfId0_f + fc_prop->cos);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        if (0 == fc_prop->enable)
        {
            CTC_BIT_UNSET(pri, fc_prop->cos);
        }
    }
    else
    {
        /*port threshold profile ID*/
        SYS_QOS_QUEUE_LOCK(lchip);
        field_val = fc_prop->enable ? p_gb_queue_master[lchip]->p_fc_profile[channel]->head.profile_id : 1;
        SYS_QOS_QUEUE_UNLOCK(lchip);
        SYS_QUEUE_DBG_INFO(" profile id = %d\n", field_val);
        field_id = DsIgrPortThrdProfId_ProfIdHigh0_f + (channel) % 8;
        index = (channel) / 8;
        cmd = DRV_IOW(DsIgrPortThrdProfId_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));

        /*enable port drop check*/
        field_val = fc_prop->enable ? 1 : 0;  /* default use profile 0 */
        index = channel;
        for (fc_prop->cos = 0; fc_prop->cos < 8; fc_prop->cos++)
        {
            cmd = DRV_IOW(DsIgrCondDisProfId_t, DsIgrCondDisProfId_ProfId0_f + fc_prop->cos);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &field_val));
        }

        if (0 == fc_prop->enable)
        {
            pri = 0;
        }
    }

    /* 3. clear port Req Status */
    cmd = DRV_IOW(PauseReqTab_t, PauseReqTab_Pri_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, port_cap.mac_id, cmd, &pri));

    return CTC_E_NONE;
}


/**
 @brief Get service queue statistics.
*/
int32
sys_greatbelt_queue_stats_query(uint8 lchip, ctc_qos_queue_stats_t* p_stats_param)
{
    sys_stats_queue_t stats;
    ctc_qos_queue_stats_info_t* p_stats = NULL;
    sys_queue_node_t* p_sys_queue_node = NULL;
    uint16 queue_id = 0;
    uint8  lport = 0;

    SYS_QUEUE_DBG_FUNC();

    if ((p_stats_param->queue.queue_type == CTC_QUEUE_TYPE_NETWORK_EGRESS)
        || (p_stats_param->queue.queue_type == CTC_QUEUE_TYPE_SERVICE_INGRESS))
    {
        SYS_MAP_GPORT_TO_LPORT(p_stats_param->queue.gport, lchip, lport);
    }

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_stats_param->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("Queue id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    SYS_QOS_QUEUE_UNLOCK(lchip);

    if (!p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_stats = &p_stats_param->stats;

    CTC_ERROR_RETURN(sys_greatbelt_stats_get_queue_stats(lchip, queue_id, &stats));
    p_stats->drop_packets = stats.queue_drop_pkts;
    p_stats->drop_bytes   = stats.queue_drop_bytes;
    p_stats->deq_packets  = stats.queue_deq_pkts;
    p_stats->deq_bytes    = stats.queue_deq_bytes;

    return CTC_E_NONE;
}

/**
 @brief Get service queue statistics.
*/
int32
sys_greatbelt_queue_stats_clear(uint8 lchip, ctc_qos_queue_stats_t* p_stats_param)
{
    sys_queue_node_t* p_sys_queue_node = NULL;
    uint16 queue_id = 0;
    uint8  lport = 0;

    SYS_QUEUE_DBG_FUNC();

    if ((p_stats_param->queue.queue_type == CTC_QUEUE_TYPE_NETWORK_EGRESS)
        || (p_stats_param->queue.queue_type == CTC_QUEUE_TYPE_SERVICE_INGRESS))
    {
        SYS_MAP_GPORT_TO_LPORT(p_stats_param->queue.gport, lchip, lport);
    }

    SYS_QOS_QUEUE_LOCK(lchip);
    CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip,
                                                       &p_stats_param->queue,
                                                       &queue_id));

    SYS_QUEUE_DBG_INFO("Queue id = %d\n", queue_id);

    p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);

    SYS_QOS_QUEUE_UNLOCK(lchip);

    if (!p_sys_queue_node)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_greatbelt_stats_clear_queue_stats(lchip, queue_id));

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_cpu_reason_init(uint8 lchip)
{
    uint8 reason = 0;
    uint8 idx = 0;
    uint8 queue_id = 0;
    uint8 group_id = 0;

    uint8 reason_grp0_que0[] =
    {
        CTC_PKT_CPU_REASON_L3_MARTIAN_ADDR,
        CTC_PKT_CPU_REASON_L3_URPF_FAIL,
        CTC_PKT_CPU_REASON_IPMC_TTL_CHECK_FAIL,
        CTC_PKT_CPU_REASON_MPLS_TTL_CHECK_FAIL,
        CTC_PKT_CPU_REASON_GRE_UNKNOWN,
        CTC_PKT_CPU_REASON_LABEL_MISS,
        CTC_PKT_CPU_REASON_LINK_ID_FAIL,
        CTC_PKT_CPU_REASON_OAM_HASH_CONFLICT
    };

    uint8 reason_grp0_que1[] =
    {
        CTC_PKT_CPU_REASON_IGMP_SNOOPING,
        CTC_PKT_CPU_REASON_PTP,
        CTC_PKT_CPU_REASON_MPLS_OAM,
        CTC_PKT_CPU_REASON_DCN,
        CTC_PKT_CPU_REASON_SCL_MATCH,
        CTC_PKT_CPU_REASON_ACL_MATCH,
        CTC_PKT_CPU_REASON_SFLOW_SOURCE,
        CTC_PKT_CPU_REASON_SFLOW_DEST
    };

    uint8 reason_grp0_que2[] =
    {
        CTC_PKT_CPU_REASON_L2_PORT_LEARN_LIMIT,
        CTC_PKT_CPU_REASON_L2_VLAN_LEARN_LIMIT,
        CTC_PKT_CPU_REASON_L2_VSI_LEARN_LIMIT,
        CTC_PKT_CPU_REASON_L2_SYSTEM_LEARN_LIMIT,
        CTC_PKT_CPU_REASON_L2_MOVE,
        CTC_PKT_CPU_REASON_L2_CPU_LEARN,
        CTC_PKT_CPU_REASON_L2_COPY_CPU,
        CTC_PKT_CPU_REASON_L2_STORM_CTL,
        CTC_PKT_CPU_REASON_IP_TTL_CHECK_FAIL,
        CTC_PKT_CPU_REASON_L3_MTU_FAIL,
        CTC_PKT_CPU_REASON_L3_MC_RPF_FAIL,
        CTC_PKT_CPU_REASON_L3_MC_MORE_RPF,
        CTC_PKT_CPU_REASON_L3_IP_OPTION,
        CTC_PKT_CPU_REASON_L3_ICMP_REDIRECT,
        CTC_PKT_CPU_REASON_L3_COPY_CPU,
        CTC_PKT_CPU_REASON_ARP_MISS,
    };

    if (p_gb_queue_master[lchip]->enq_mode)
    {
        /*for alloc queue*/
        for (group_id = 0; group_id < 16; group_id++)
        {
            CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, CTC_PKT_CPU_REASON_L2_PDU, 0, group_id));
        }
        CTC_ERROR_RETURN(sys_greatbelt_queue_c2c_init(lchip, p_gb_queue_master[lchip]->c2c_group_base));
    }

    for (idx = 0; idx < CTC_PKT_CPU_REASON_L2_PDU_RESV; idx++)
    {
        reason = CTC_PKT_CPU_REASON_L2_PDU + idx;
        queue_id = 0;
        group_id = 1;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
    }

    for (idx = 0; idx < CTC_PKT_CPU_REASON_L3_PDU_RESV; idx++)
    {
        reason = CTC_PKT_CPU_REASON_L3_PDU + idx;
        queue_id = 0;
        group_id = 2;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
    }

    for (idx = 0; idx < sizeof(reason_grp0_que0) / sizeof(uint8); idx++)
    {
        reason = reason_grp0_que0[idx];
        queue_id = 0;
        group_id = 0;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
        CTC_ERROR_RETURN(sys_greatbelt_cpu_reason_set_dest(lchip,
                                                           reason,
                                                           CTC_PKT_CPU_REASON_TO_DROP,
                                                           0));
    }

    for (idx = 0; idx < sizeof(reason_grp0_que1) / sizeof(uint8); idx++)
    {
        reason = reason_grp0_que1[idx];
        queue_id = 1;
        group_id = 0;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
    }

    for (idx = 0; idx < sizeof(reason_grp0_que2) / sizeof(uint8); idx++)
    {
        reason = reason_grp0_que2[idx];
        queue_id = 2;
        group_id = 0;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
    }

    /*OAM PDU*/
    for (idx = 0; idx < CTC_PKT_CPU_REASON_OAM_PDU_RESV; idx++)
    {
        reason = CTC_PKT_CPU_REASON_OAM + idx;
        queue_id = 0;
        group_id = 0;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
    }

    reason = CTC_PKT_CPU_REASON_FWD_CPU;
    queue_id = 0;
    group_id = 0;
    CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));

    reason = CTC_PKT_CPU_REASON_MIRRORED_TO_CPU;
    queue_id = 0;
    group_id = 13;
    CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));

    if (p_gb_queue_master[lchip]->enq_mode)
    {
        reason = CTC_PKT_CPU_REASON_C2C_PKT;
        queue_id = 0;
        group_id = p_gb_queue_master[lchip]->c2c_group_base;
        CTC_ERROR_RETURN(sys_greatbelt_excp_queue_add(lchip, reason, queue_id, group_id));
    }

    CTC_ERROR_RETURN(_sys_greatbelt_cpu_reason_mode_init(lchip));

    return CTC_E_NONE;
}

int32
sys_greatbelt_qos_queue_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail)
{
    uint16 queue_id = 0;
    uint8  is_pps = 0;
    sys_queue_node_t* p_sys_queue_node = NULL;

    SYS_QUEUE_INIT_CHECK(lchip);
    SYS_QUEUE_DBG_DUMP("show queue information:\n");
    SYS_QUEUE_DBG_DUMP("============================================\n");
    SYS_QUEUE_DBG_DUMP("%-7s %-7s %-7s %-7s", "queue", "offset", "channel", "group");

    if (detail)
    {
        SYS_QUEUE_DBG_DUMP("%-10s %-10s", "stats-en", "shape-en");
        SYS_QUEUE_DBG_DUMP("%-13s %-13s %-13s %-13s %-10s", "cir(kbps/pps)", "pir(kbps/pps)", "cbs(kb/pkt)", "pbs(kb/pkt)", "shp_mode");
        SYS_QUEUE_DBG_DUMP("%-10s %-10s %-10s %-10s", "class(C)", "class(E)", "weight(C)", "weight(E)");
    }

    SYS_QUEUE_DBG_DUMP("\n");

    SYS_QOS_QUEUE_LOCK(lchip);
    for (queue_id = start; queue_id <= end; queue_id++)
    {
        p_sys_queue_node = ctc_vector_get(p_gb_queue_master[lchip]->queue_vec, queue_id);
        if (NULL == p_sys_queue_node)
        {
            continue;
        }

        SYS_QUEUE_DBG_DUMP("%-7d %-7d %-7d %-7d",
                           p_sys_queue_node->queue_id,
                           p_sys_queue_node->offset,
                           p_sys_queue_node->channel,
                           p_sys_queue_node->group_id);

        if (detail)
        {
            if ((p_sys_queue_node->channel == p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_CPU].queue_info.channel)
                || (p_sys_queue_node->channel == p_gb_queue_master[lchip]->channel[SYS_RESERVE_PORT_ID_DMA].queue_info.channel) )
            {
                is_pps = p_gb_queue_master[lchip]->shp_pps_en;
            }
            SYS_QUEUE_DBG_DUMP("%-10d %-10d",
                               p_sys_queue_node->stats_en,
                               p_sys_queue_node->shp_en);

            SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, p_sys_queue_node->cbs);
            SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, p_sys_queue_node->pbs);
            SYS_QUEUE_DBG_DUMP("%-13d %-13d %-13d %-13d %-10s",
                               p_sys_queue_node->cir,
                               p_sys_queue_node->pir,
                               p_sys_queue_node->cbs,
                               p_sys_queue_node->pbs,
                               is_pps?"PPS":"BPS");

            SYS_QUEUE_DBG_DUMP("%-10d %-10d %-10d %-10d",
                               p_sys_queue_node->confirm_class,
                               p_sys_queue_node->exceed_class,
                               p_sys_queue_node->confirm_weight,
                               p_sys_queue_node->exceed_weight);
        }

        SYS_QUEUE_DBG_DUMP("\n");

    }
    SYS_QOS_QUEUE_UNLOCK(lchip);

    if ((start == end) && detail)
    {
        uint8 wred = 0 ;
        uint16 wredMaxThrd[4];
        uint16 wredMinThrd[4];
        uint16 drop_prob[4];
        uint8 cngest = 0;
        uint32 cmd = 0;
        uint32 value = 0;
        ds_egr_resrc_ctl_t ds_egr_resrc_ctl;
        ds_que_thrd_profile_t ds_que_thrd_profile;

        sal_memset(&ds_que_thrd_profile, 0, sizeof(ds_que_thrd_profile_t));
        sal_memset(&ds_egr_resrc_ctl, 0, sizeof(ds_egr_resrc_ctl_t));

        SYS_QUEUE_DBG_DUMP("\nQueue Drop:\n");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        queue_id = start;
        cmd = DRV_IOR(DsEgrResrcCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &ds_egr_resrc_ctl));

        wred = ds_egr_resrc_ctl.wred_drop_mode;
        SYS_QUEUE_DBG_DUMP("%-30s: %s\n", "Drop Mode", wred? "WRED":"WTD");

        value = ds_egr_resrc_ctl.mapped_tc;
        SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Mapped tc", value);
        value = ds_egr_resrc_ctl.mapped_sc;
        SYS_QUEUE_DBG_DUMP("%-30s: %d\n", "Mapped sc", value);
        value = ds_egr_resrc_ctl.que_thrd_prof_id_high;

        for (cngest = 0; cngest < 8; cngest++)
        {
            cmd = DRV_IOR(DsQueThrdProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, (value << 3) + cngest, cmd, &ds_que_thrd_profile));

            wredMaxThrd[0] = ds_que_thrd_profile.wred_max_thrd0;
            wredMaxThrd[1] = ds_que_thrd_profile.wred_max_thrd1;
            wredMaxThrd[2] = ds_que_thrd_profile.wred_max_thrd2;
            wredMaxThrd[3] = ds_que_thrd_profile.wred_max_thrd3;
            wredMinThrd[0] = ds_que_thrd_profile.wred_min_thrd0;
            wredMinThrd[1] = ds_que_thrd_profile.wred_min_thrd1;
            wredMinThrd[2] = ds_que_thrd_profile.wred_min_thrd2;
            wredMinThrd[3] = ds_que_thrd_profile.wred_min_thrd3;
            drop_prob[0] = ds_que_thrd_profile.max_drop_prob0;
            drop_prob[1] = ds_que_thrd_profile.max_drop_prob1;
            drop_prob[2] = ds_que_thrd_profile.max_drop_prob2;
            drop_prob[3] = ds_que_thrd_profile.max_drop_prob3;

            if (wred)
            {
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u\n", cngest, "MaxThrd",  wredMaxThrd[0], wredMaxThrd[1], wredMaxThrd[2]);
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u\n", cngest, "MinThrd",  wredMinThrd[0], wredMinThrd[1], wredMinThrd[2]);
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u \n", cngest, "Drop_prob", drop_prob[0], drop_prob[1], drop_prob[2]);
            }
            else
            {
                SYS_QUEUE_DBG_DUMP("Congest:%d %-19s : /%-8u/%-8u/%-8u\n", cngest, "DropThrd", wredMaxThrd[0], wredMaxThrd[1], wredMaxThrd[2]);
            }

        }

    }

    SYS_QUEUE_DBG_DUMP("============================================\n");

    return CTC_E_NONE;

}

int32
sys_greatbelt_qos_group_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail)
{
    uint16 group_id = 0;
    sys_queue_group_node_t* p_sys_group_node = NULL;
    ctc_slist_t* p_queue_list = NULL;
    sys_queue_node_t* p_queue_node = NULL;
    ctc_slistnode_t* ctc_slistnode = NULL;

    SYS_QUEUE_INIT_CHECK(lchip);
    SYS_QUEUE_DBG_DUMP("show group information:\n");
    SYS_QUEUE_DBG_DUMP("============================================\n");
    SYS_QUEUE_DBG_DUMP("%-10s %-10s", "group", "start-que");
    SYS_QUEUE_DBG_DUMP("%-4s %-4s %-4s %-4s ", "que0", "que1", "que2", "que3");
    SYS_QUEUE_DBG_DUMP("%-4s %-4s %-4s %-4s",  "que4", "que5", "que6", "que7");
    SYS_QUEUE_DBG_DUMP("\n");

    SYS_QOS_QUEUE_LOCK(lchip);
    for (group_id = start; group_id <= end; group_id++)
    {
        p_sys_group_node = ctc_vector_get(p_gb_queue_master[lchip]->group_vec, group_id);
        if (NULL == p_sys_group_node)
        {
            continue;
        }

        SYS_QUEUE_DBG_DUMP("%-10d %-10d",
                           group_id,
                           p_sys_group_node->start_queue_id);

        p_queue_list = p_sys_group_node->p_queue_list;

        CTC_SLIST_LOOP(p_queue_list, ctc_slistnode)
        {
            p_queue_node = _ctc_container_of(ctc_slistnode, sys_queue_node_t, head);

            SYS_QUEUE_DBG_DUMP("%-4d ",
                               p_queue_node->queue_id);

        }

        SYS_QUEUE_DBG_DUMP("\n");

    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    SYS_QUEUE_DBG_DUMP("============================================\n");

    return CTC_E_NONE;

}

int32
sys_greatbelt_qos_channel_dump(uint8 lchip, uint32 start, uint32 end, uint8 detail)
{
    uint8 lport   = 0;
    sys_queue_channel_t* p_channel = NULL;
    uint8 cpu_port_en = 0;
    uint16 lport_start = 0;
    uint16 lport_end = 0;
    uint8  is_pps = 0;

    if (start > end)
    {
        return CTC_E_INVALID_CONFIG;
    }

    if ((start == end) && CTC_IS_CPU_PORT(start))
    {
        SYS_QUEUE_INIT_CHECK(lchip);
        SYS_QUEUE_DBG_DUMP("show channel information:\n");
        SYS_QUEUE_DBG_DUMP("============================================================\n");
        SYS_QUEUE_DBG_DUMP("%-13s %-10s", "port   ","channel");
        SYS_QUEUE_DBG_DUMP("%-10s %-13s %-13s %-10s %-10s", "shape-en", "pir(kbps/pps)", "pbs(kb/pkt)", "group", "shp_mode");
        SYS_QUEUE_DBG_DUMP("\n");
        CTC_ERROR_RETURN(sys_greatbelt_chip_get_cpu_eth_en(lchip, &cpu_port_en));
        lport = cpu_port_en ? SYS_RESERVE_PORT_ID_CPU : SYS_RESERVE_PORT_ID_DMA;
        SYS_MAP_GCHIP_TO_LCHIP((SYS_MAP_GPORT_TO_GCHIP(start)), lchip);
        SYS_QUEUE_DBG_DUMP("0x%-11x ", start);

        SYS_QOS_QUEUE_LOCK(lchip);
        p_channel = &p_gb_queue_master[lchip]->channel[lport];
        SYS_QOS_QUEUE_UNLOCK(lchip);
        is_pps = p_gb_queue_master[lchip]->shp_pps_en;
        SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, p_channel->pbs);
        SYS_QUEUE_DBG_DUMP("%-10d %-10d %-13d %-13d %-10d %-10s",
                           p_channel->queue_info.channel,
                           p_channel->shp_en,
                           p_channel->pir,
                           p_channel->pbs,
                           p_channel->queue_info.group_id,
                           is_pps?"PPS":"BPS");

        SYS_QUEUE_DBG_DUMP("\n");
        SYS_QUEUE_DBG_DUMP("============================================================\n");

        return CTC_E_NONE;
    }

    SYS_MAP_GPORT_TO_LPORT(start, lchip, lport_start);
    SYS_MAP_GPORT_TO_LPORT(end, lchip, lport_end);

    if ((lport_start >= SYS_MAX_LOCAL_PORT) || (lport_end >= SYS_MAX_LOCAL_PORT))
    {
        return CTC_E_INVALID_PORT;
    }
    SYS_QUEUE_INIT_CHECK(lchip);
    SYS_QUEUE_DBG_DUMP("show channel information:\n");
    SYS_QUEUE_DBG_DUMP("============================================================\n");
    SYS_QUEUE_DBG_DUMP("%-10s %-10s", "port ","channel");
    SYS_QUEUE_DBG_DUMP("%-10s %-13s %-13s %-10s %-10s", "shape-en", "pir(kbps/pps)", "pbs(kb/pkt)", "group", "shp_mode");
    SYS_QUEUE_DBG_DUMP("\n");

    SYS_QOS_QUEUE_LOCK(lchip);
    for (lport = lport_start; lport <= lport_end; lport++)
    {
        SYS_QUEUE_DBG_DUMP("0x%04x     ",lport);
        p_channel = &p_gb_queue_master[lchip]->channel[lport];
        if ((lport == SYS_RESERVE_PORT_ID_CPU)||(lport == SYS_RESERVE_PORT_ID_DMA))
        {
            is_pps = p_gb_queue_master[lchip]->shp_pps_en;
        }
        SYS_QOS_GET_SHAPE_TOKEN_THRD(is_pps, p_channel->pbs);
        SYS_QUEUE_DBG_DUMP("%-10d %-10d %-13d %-13d %-10d %-10s",
                           p_channel->queue_info.channel,
                           p_channel->shp_en,
                           p_channel->pir,
                           p_channel->pbs,
                           p_channel->queue_info.group_id,
                           is_pps?"PPS":"BPS");

        SYS_QUEUE_DBG_DUMP("\n");

    }

    if (start == end)
    {
        uint16 queue_id = 0;
        uint16 queue_index = 0;
        uint32 queue_depth = 0;
        uint32 cmd = 0;
        ctc_qos_queue_id_t queue;
        sal_memset(&queue, 0, sizeof(ctc_qos_queue_id_t));

        queue.queue_type = CTC_QUEUE_TYPE_NETWORK_EGRESS;
        queue.gport = start;

        SYS_QUEUE_DBG_DUMP("\n%-10s %-10s\n", "queue-id", "queue-depth");
        SYS_QUEUE_DBG_DUMP("-------------------------------\n");

        SYS_MAP_GPORT_TO_LPORT(start, lchip, lport);

        for (queue_index = 0; queue_index < p_gb_queue_master[lchip]->channel[lport].queue_info.queue_num; queue_index++)
        {
            queue.queue_id = queue_index;
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip, &queue, &queue_id));

            cmd = DRV_IOR(DsQueCnt_t, DsQueCnt_QueInstCnt_f);
            CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(DRV_IOCTL(lchip, queue_id, cmd, &queue_depth));

            SYS_QUEUE_DBG_DUMP("%-10d %-10d\n", queue_id, queue_depth);

        }
    }

    SYS_QOS_QUEUE_UNLOCK(lchip);

    SYS_QUEUE_DBG_DUMP("============================================================\n");

    return CTC_E_NONE;

}


int32
sys_greatbelt_qos_service_dump(uint8 lchip, uint16 service_id, uint32 gport, uint8 detail)
{
    uint16 queue_id = 0;
    uint16 queue_index = 0;
    uint32 queue_depth = 0;
    uint32 cmd = 0;
    ctc_qos_queue_id_t queue;
    sal_memset(&queue, 0, sizeof(ctc_qos_queue_id_t));

    SYS_QUEUE_INIT_CHECK(lchip);
    SYS_QUEUE_DBG_DUMP("show service information:\n");
    SYS_QUEUE_DBG_DUMP("============================================================\n");

    SYS_QOS_QUEUE_LOCK(lchip);

    queue.queue_type = CTC_QUEUE_TYPE_SERVICE_INGRESS;
    queue.gport = gport;
    queue.service_id = service_id;

    SYS_QUEUE_DBG_DUMP("%-10s %-10s\n", "queue-id", "queue-depth");
    SYS_QUEUE_DBG_DUMP("-------------------------------\n");

    for (queue_index = 0; queue_index < p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_SERVICE].queue_num; queue_index++)
    {
        queue.queue_id = queue_index;
        CTC_ERROR_RETURN_QOS_QUEUE_UNLOCK(_sys_greatbelt_queue_get_queue_id(lchip, &queue, &queue_id));

        cmd = DRV_IOR(DsQueCnt_t, DsQueCnt_QueInstCnt_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, queue_id, cmd, &queue_depth));

        SYS_QUEUE_DBG_DUMP("%-10d %-10d\n", queue_id, queue_depth);

    }

    SYS_QOS_QUEUE_UNLOCK(lchip);

    SYS_QUEUE_DBG_DUMP("============================================================\n");

    return CTC_E_NONE;

}


int32
sys_greatbelt_queue_get_queue_num(uint8 lchip, sys_queue_type_t queue_type)
{
    SYS_QUEUE_INIT_CHECK(lchip);
    return p_gb_queue_master[lchip]->queue_num_ctl[queue_type].queue_num;
}

uint8
sys_greatbelt_queue_get_enqueue_mode(uint8 lchip)
{
    return p_gb_queue_master[lchip]->enq_mode;
}

int32
sys_greatbelt_queue_get_c2c_queue_base(uint8 lchip, uint16* c2c_queue_base)
{
    *c2c_queue_base = p_gb_queue_master[lchip]->c2c_group_base*SYS_REASON_GRP_QUEUE_NUM;
    return CTC_E_NONE;
}

/**
 @brief Globally set service queue mode.
  mode 0: 4 queue mode
  mode 1: 8 queue mode
*/
int32
sys_greatbelt_queue_set_service_queue_mode(uint8 lchip, uint8 mode)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_MAX_VALUE_CHECK(mode, 1);

    SYS_QOS_QUEUE_LOCK(lchip);
    if (mode)
    {
        p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_SERVICE].queue_num = 8;
    }
    else
    {
        p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_SERVICE].queue_num = 4;
    }
    SYS_QOS_QUEUE_UNLOCK(lchip);
    return CTC_E_NONE;
}


/**
 @brief Globally set c2c queue mode.
  mode 0: c2c pkt use 8 queue(8-15) in trunk port
  mode 1: c2c pkt use 16 queue(0-15) in trunk port
*/
int32
sys_greatbelt_queue_set_c2c_queue_mode(uint8 lchip, uint8 mode)
{
    SYS_QUEUE_INIT_CHECK(lchip);
    CTC_MAX_VALUE_CHECK(mode, 1);

    if (mode)
    {
        p_gb_queue_master[lchip]->c2c_enq_mode = 1;
    }
    else
    {
        p_gb_queue_master[lchip]->c2c_enq_mode = 0;
    }

    return CTC_E_NONE;
}

/**
 @brief En-queue component initialization.
*/
int32
sys_greatbelt_queue_enq_init(uint8 lchip, void *p_glb_parm)
{
    uint8  lport = 0;
    uint8  drop_chan = 0;
    sys_greatbelt_opf_t opf;

    ctc_qos_global_cfg_t * p_glb_cfg = NULL;
    uint8 c2c_group_base = 14;/* 0~15 CPU group*/

    p_glb_cfg = (ctc_qos_global_cfg_t *)p_glb_parm;


    /*************************************************
    *init global prarm
    *************************************************/
    if (p_gb_queue_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gb_queue_master[lchip] = mem_malloc(MEM_QUEUE_MODULE, sizeof(sys_queue_master_t));
    if (NULL == p_gb_queue_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gb_queue_master[lchip], 0, sizeof(sys_queue_master_t));
    SYS_QOS_QUEUE_CREATE_LOCK(lchip);
    /* default use priority_mode : 1  0-15 priority */
    p_gb_queue_master[lchip]->priority_mode = (p_glb_cfg->priority_mode == 0)? 1:0;

    p_gb_queue_master[lchip]->queue_vec =
        ctc_vector_init(SYS_QUEUE_VEC_BLOCK_NUM, SYS_QUEUE_VEC_BLOCK_SIZE);

    p_gb_queue_master[lchip]->group_vec =
        ctc_vector_init(SYS_QUEUE_GRP_VEC_BLOCK_NUM, SYS_QUEUE_GRP_VEC_BLOCK_SIZE);

    p_gb_queue_master[lchip]->reason_vec =
        ctc_vector_init(SYS_REASON_VEC_BLOCK_NUM, SYS_REASON_VEC_BLOCK_SIZE);


    p_gb_queue_master[lchip]->p_queue_hash =
        ctc_hash_create(
        SYS_QUEUE_HASH_BLOCK_NUM,
        SYS_QUEUE_HASH_BLOCK_SIZE,
        (hash_key_fn)sys_greatbelt_queue_info_hash_make,
        (hash_cmp_fn)sys_greatbelt_queue_info_hash_cmp);


    p_gb_queue_master[lchip]->p_service_hash =
    ctc_hash_create(
            SYS_SERVICE_HASH_BLOCK_NUM,
            SYS_SERVICE_HASH_BLOCK_SIZE,
            (hash_key_fn)sys_greatbelt_service_hash_make,
            (hash_cmp_fn)sys_greatbelt_service_hash_cmp);



    /*************************************************
    *init opf for related tables
    *************************************************/

    sys_greatbelt_opf_init(lchip, OPF_QUEUE, 1);
    sys_greatbelt_opf_init(lchip, OPF_QUEUE_GROUP, 1);

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    opf.pool_index = 0;
    opf.pool_type = OPF_QUEUE;
    sys_greatbelt_opf_init_offset(lchip, &opf, 0, SYS_MAX_QUEUE_NUM);

    opf.pool_type = OPF_QUEUE_GROUP;
    sys_greatbelt_opf_init_offset(lchip, &opf, 0, SYS_MAX_QUEUE_GROUP_NUM-1);


    /*************************************************
    *init group for cpu reason
    *************************************************/
    CTC_ERROR_RETURN(_sys_greatbelt_group_rsv_for_cpu_reason(lchip));

    /*************************************************
    *init drop channel
    *************************************************/
    CTC_ERROR_RETURN(_sys_greatbelt_rsv_channel_drop_init(lchip));

    /*************************************************
    *init drop channel
    *************************************************/
    CTC_ERROR_RETURN(sys_greatbelt_qos_resrc_mgr_init(lchip, p_glb_cfg));

    /*************************************************
    *init queue stats base
    *************************************************/
    CTC_ERROR_RETURN(sys_greatbelt_stats_set_queue_en(lchip, 0));


    CTC_ERROR_RETURN(_sys_greatbelt_queue_select_init(lchip));

    /*************************************************
    *init queue alloc
    *************************************************/
    if (8 == p_glb_cfg->queue_num_per_network_port)
    {
        p_gb_queue_master[lchip]->enq_mode = 1;
    }
    p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_NORMAL].queue_num   = p_glb_cfg->queue_num_per_network_port;
    p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_STACKING].queue_num = p_glb_cfg->queue_num_per_network_port;
    p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_INTERNAL].queue_num = p_glb_cfg->queue_num_per_internal_port;
    p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_EXCP].queue_num     = 1;
    p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_SERVICE].queue_num  = p_glb_cfg->queue_num_per_ingress_service;
    p_gb_queue_master[lchip]->cpu_reason_grp_que_num                           = p_glb_cfg->queue_num_per_cpu_reason_group;
    p_gb_queue_master[lchip]->c2c_group_base = c2c_group_base;

    /* init internal drop lport first */
     /*drop_chan =  SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DROP);*/
    /*if drop port no channel, use queue resource drop*/
    drop_chan =  SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_ELOOP);


    CTC_ERROR_RETURN(sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_INTERNAL, SYS_RESERVE_PORT_ID_DROP, drop_chan));

    /* init all pannel port */
    if ( 0 != p_gb_queue_master[lchip]->queue_num_ctl[SYS_QUEUE_TYPE_NORMAL].queue_num)
    {
        for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
        {
            CTC_ERROR_RETURN(sys_greatbelt_port_queue_add(lchip, SYS_QUEUE_TYPE_NORMAL, lport, 0));
        }
    }

    if(p_glb_cfg->queue_aging_time)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_queue_aging(lchip, p_glb_cfg->queue_aging_time));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_queue_free_node_data(void* node_data, void* user_data)
{
    uint32 free_group_vector = 0;

    if (user_data)
    {
        free_group_vector = *(uint32*)user_data;
        if (1 == free_group_vector)
        {
            ctc_slist_free(((sys_queue_group_node_t*)node_data)->p_queue_list);
        }
    }

    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_queue_deinit(uint8 lchip)
{
    uint32 free_group_vector = 1;

    if (NULL == p_gb_queue_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free drop profile data*/
    ctc_spool_free(p_gb_queue_master[lchip]->p_drop_profile_pool);

    sys_greatbelt_opf_deinit(lchip, OPF_QUEUE_DROP_PROFILE);

    /*free gruop profile data*/
    ctc_spool_free(p_gb_queue_master[lchip]->p_group_profile_pool);

    /*free queue profile data*/
    ctc_spool_free(p_gb_queue_master[lchip]->p_queue_profile_pool);

    sys_greatbelt_opf_deinit(lchip, OPF_QUEUE_SHAPE_PROFILE);
    sys_greatbelt_opf_deinit(lchip, OPF_GROUP_SHAPE_PROFILE);

    sys_greatbelt_opf_deinit(lchip, OPF_QUEUE);
    sys_greatbelt_opf_deinit(lchip, OPF_QUEUE_GROUP);

    /*free service hash*/
    ctc_hash_traverse(p_gb_queue_master[lchip]->p_service_hash, (vector_traversal_fn)_sys_greatbelt_queue_free_node_data, NULL);
    ctc_hash_free(p_gb_queue_master[lchip]->p_service_hash);

    /*free queue hash*/
    ctc_hash_traverse(p_gb_queue_master[lchip]->p_queue_hash, (vector_traversal_fn)_sys_greatbelt_queue_free_node_data, NULL);
    ctc_hash_free(p_gb_queue_master[lchip]->p_queue_hash);

    /*free reason vec*/
    ctc_vector_traverse(p_gb_queue_master[lchip]->reason_vec, (vector_traversal_fn)_sys_greatbelt_queue_free_node_data, NULL);
    ctc_vector_release(p_gb_queue_master[lchip]->reason_vec);

    /*free group vec*/
    ctc_vector_traverse(p_gb_queue_master[lchip]->group_vec, (vector_traversal_fn)_sys_greatbelt_queue_free_node_data, &free_group_vector);
    ctc_vector_release(p_gb_queue_master[lchip]->group_vec);

    /*free queue vec*/
    ctc_vector_traverse(p_gb_queue_master[lchip]->queue_vec, (hash_traversal_fn)_sys_greatbelt_queue_free_node_data, NULL);
    ctc_vector_release(p_gb_queue_master[lchip]->queue_vec);

    sal_mutex_destroy(p_gb_queue_master[lchip]->mutex);

    mem_free(p_gb_queue_master[lchip]);

    return CTC_E_NONE;
}

