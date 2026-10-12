#ifndef _CTC_GOLDENGATE_DKIT_TCAM_H_
#define _CTC_GOLDENGATE_DKIT_TCAM_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "sal.h"

#define DKITS_TCAM_PER_TYPE_LKP_NUM 4

#define IGS_ACL0_TCAM_TBL(tblid)    ((DsAclQosL3Key160Ingr0_t == tblid) || (DsAclQosMacKey160Ingr0_t == tblid) \
                                    || (DsAclQosL3Key320Ingr0_t == tblid) || (DsAclQosIpv6Key640Ingr0_t == tblid))
#define IGS_ACL1_TCAM_TBL(tblid)    ((DsAclQosL3Key160Ingr1_t == tblid) || (DsAclQosMacKey160Ingr1_t == tblid) \
                                    || (DsAclQosL3Key320Ingr1_t == tblid) || (DsAclQosIpv6Key640Ingr1_t == tblid))
#define IGS_ACL2_TCAM_TBL(tblid)    ((DsAclQosL3Key160Ingr2_t == tblid) || (DsAclQosMacKey160Ingr2_t == tblid) \
                                    || (DsAclQosL3Key320Ingr2_t == tblid) || (DsAclQosIpv6Key640Ingr2_t == tblid))
#define IGS_ACL3_TCAM_TBL(tblid)    ((DsAclQosL3Key160Ingr3_t == tblid) || (DsAclQosMacKey160Ingr3_t == tblid) \
                                    || (DsAclQosL3Key320Ingr3_t == tblid) || (DsAclQosIpv6Key640Ingr3_t == tblid))
#define IGS_SCL0_TCAM_TBL(tblid)    ((DsUserId0MacKey160_t == tblid) || (DsUserId0L3Key320_t == tblid) \
                                    || (DsUserId0L3Key160_t == tblid) || (DsUserId0Ipv6Key640_t == tblid))
#define IGS_SCL1_TCAM_TBL(tblid)    ((DsUserId1MacKey160_t == tblid) || (DsUserId1L3Key320_t == tblid) \
                                    || (DsUserId1L3Key160_t == tblid) || (DsUserId1Ipv6Key640_t == tblid))
#define EGS_ACL_TCAM_TBL(tblid)     ((DsAclQosMacKey160Egr0_t == tblid) || (DsAclQosL3Key160Egr0_t == tblid) \
                                    || (DsAclQosL3Key320Egr0_t == tblid) || (DsAclQosIpv6Key640Egr0_t == tblid))
#define IP_PREFIX_TCAM_TBL(tblid)   ((DsLpmTcamIpv440Key_t == tblid) || (DsLpmTcamIpv6160Key0_t == tblid))
#define IP_NAT_PBR_TCAM_TBL(tblid)  ((DsLpmTcamIpv4Pbr160Key_t == tblid) || (DsLpmTcamIpv6320Key_t == tblid) \
                                    || (DsLpmTcamIpv6160Key1_t == tblid) || (DsLpmTcamIpv4NAT160Key_t == tblid))

enum dkits_tcam_block_type_e
{
    DKITS_TCAM_BLOCK_TYPE_IGS_ACL0,
    DKITS_TCAM_BLOCK_TYPE_IGS_ACL1,
    DKITS_TCAM_BLOCK_TYPE_IGS_ACL2,
    DKITS_TCAM_BLOCK_TYPE_IGS_ACL3,

    DKITS_TCAM_BLOCK_TYPE_EGS_ACL0,
    DKITS_TCAM_BLOCK_TYPE_IGS_USERID0,
    DKITS_TCAM_BLOCK_TYPE_IGS_USERID1,

    DKITS_TCAM_BLOCK_TYPE_IGS_LPM0,
    DKITS_TCAM_BLOCK_TYPE_IGS_LPM1,

    DKITS_TCAM_BLOCK_TYPE_NUM
};
typedef enum dkits_tcam_block_type_e dkits_tcam_block_type_t;

extern char* gg_tcam_module_desc[];

extern int32
ctc_goldengate_dkits_tcam_capture_start(void*);

extern int32
ctc_goldengate_dkits_tcam_capture_stop(void*);

extern int32
ctc_goldengate_dkits_tcam_capture_show(void*);

extern int32
ctc_goldengate_dkits_tcam_scan(void*);

extern int32
ctc_goldengate_dkits_tcam_show_key_type(uint8);

extern int32
ctc_goldengate_dkits_tcam_mem2tbl(uint32, uint32, uint32*);

extern int32
ctc_goldengate_dkits_tcam_parser_key_type(tbls_id_t, tbl_entry_t*, uint32*);

extern int32
ctc_goldengate_dkits_tcam_read_tcam_key(uint8 lchip, tbls_id_t, uint32, uint32*, tbl_entry_t*);

extern int32
ctc_goldengate_dkits_tcam_entry_offset(tbls_id_t, uint32, uint32*, uint32*);

#ifdef __cplusplus
}
#endif

#endif

