#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_register.h"

#undef DRV_DEF_C
#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F

#ifdef DRV_DEF_C
	#error DRV_DEF_C has been defined
#endif

#ifdef DRV_DEF_M
	#error DRV_DEF_M has been defined
#endif

#ifdef DRV_DEF_D
	#error DRV_DEF_D has been defined
#endif

#ifdef DRV_DEF_F
	#error DRV_DEF_F has been defined
#endif


 /*Field Addr*/
#define CTC_FIELD_ADDR(ModuleName, RegName, FieldName, Bits, ...) \
  static segs_t RegName##_##FieldName##_tbl_segs[] = {__VA_ARGS__};

 /*Field Info*/
#define CTC_FIELD_INFO(ModuleName, RegName, FieldName, Bits, ...) \
   { \
      #FieldName, \
      Bits, \
      sizeof(RegName##_##FieldName##_tbl_segs) / sizeof(segs_t), \
      RegName##_##FieldName##_tbl_segs, \
   },

 /*DS Field List Seperator*/
#define CTC_DS_SEPERATOR_INFO(ModuleName, RegName, SliceType, OpType, Entry, Word, ...) \
 }; \
 static fields_t RegName##_tbl_fields[] = {




#define DRV_DEF_C(MaxInstNum, MaxEntry, MaxWord, MaxBits,MaxStartPos,MaxSegSize)

 /* Segment Info*/
#define DRV_DEF_M(ModuleName, InstNum)
#define DRV_DEF_D(ModuleName, InstNum, RegName, SliceType, OpType, Entry, Word, ...)
#define DRV_DEF_F(ModuleName, InstNum, RegName, FieldName, Bits, ...) \
        CTC_FIELD_ADDR(ModuleName, RegName, FieldName, Bits, __VA_ARGS__)
#include "goldengate/include/drv_ds.h"
#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F

 /* Field Info*/
#define DRV_DEF_M(ModuleName, InstNum)
#define DRV_DEF_D(ModuleName, InstNum, RegName, SliceType, OpType, Entry, Word, ...) \
        CTC_DS_SEPERATOR_INFO(ModuleName, RegName, SliceType, OpType, Entry, Word, __VA_ARGS__)
#define DRV_DEF_F(ModuleName, InstNum, RegName, FieldName, Bits, ...) \
        CTC_FIELD_INFO(ModuleName, RegName, FieldName, Bits, __VA_ARGS__)

fields_t gg_fields_1st = {NULL,0,0,NULL
#include "goldengate/include/drv_ds.h"
};
#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F

 /* DS Address*/
#define DRV_DEF_M(ModuleName, InstNum)
#define DRV_DEF_D(ModuleName, InstNum, RegName, SliceType, OpType, Entry, Word, ...) \
        CTC_DS_ADDR(ModuleName, RegName, SliceType, OpType, Entry, Word, __VA_ARGS__)
#define DRV_DEF_F(ModuleName, InstNum, RegName, FieldName, Bits, ...)
#include "goldengate/include/drv_ds.h"
#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F


 /* DS List*/
#define DRV_DEF_M(ModuleName, InstNum)
#define DRV_DEF_D(ModuleName, InstNum, RegName, SliceType, OpType, Entry, Word, ...) \
        CTC_DS_INFO(ModuleName, RegName, SliceType, OpType, Entry, Word, __VA_ARGS__)
#define DRV_DEF_F(ModuleName, InstNum, RegName, FieldName, Bits, ...)

tables_info_t drv_gg_tbls_list[] = {
#include "goldengate/include/drv_ds.h"
};


#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F

#if 0

 /* Ds in Module*/
#define DRV_DEF_M(ModuleName, InstNum) \
        CTC_MODULE_SEPERATOR(ModuleName, InstNum)
#define DRV_DEF_D(ModuleName, InstNum, RegName, SliceType, OpType, Entry, Word, ...) \
        CTC_MODULE_DS(ModuleName, RegName, SliceType, OpType, Entry, Word, __VA_ARGS__)
#define DRV_DEF_F(ModuleName, InstNum, RegName, FieldName, Bits, ...)
{
#include "goldengate/include/drv_ds.h"
};
#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F


 /* Module Info*/
#define DRV_DEF_M(ModuleName, InstNum) \
        CTC_MODULE_INFO(ModuleName, InstNum)
#define DRV_DEF_D(ModuleName, InstNum, RegName, SliceType, OpType, Entry, Word, ...)
#define DRV_DEF_F(ModuleName, InstNum, RegName, FieldName, Bits, ...)
dbg_modules_t dbg_modules_list[] = {
#include "goldengate/include/drv_ds.h"
};
#endif

#undef DRV_DEF_M
#undef DRV_DEF_D
#undef DRV_DEF_F
#undef DRV_DEF_C
