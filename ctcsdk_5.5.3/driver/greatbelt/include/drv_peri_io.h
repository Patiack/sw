/**
 @file drv_chip_io.h

 @date 2011-10-09

 @version v4.28.2

 The file contains all periphery device access interface
*/
#ifndef _DRV_PERI_H
#define _DRV_PERI_H

#define DRV_1G_MDIO_READ 0x2
#define DRV_XG_MDIO_READ 0x3

#define DRV_MDIO_WRITE    0x1
#define DRV_MAX_GPIO_NUM 16
#define DRV_MAX_PHY_PORT 56

#define DRV_FLAG_ISSET(VAL, FLAG)        (((VAL)&(FLAG)) == (FLAG))
#define DRV_FLAG_ISZERO(VAL)        ((VAL) == 0)

#define DRV_LED_FORCE_OFF  0
#define DRV_LED_ON_RX_LINK 1
#define DRV_LED_ON_TX_LINK 2
#define DRV_LED_FORCE_ON 3

#define DRV_LED_BLINK_OFF 0
#define DRV_LED_BLINK_RX_ACT 1
#define DRV_LED_BLINK_TX_ACT 2
#define DRV_LED_BLINK_ON 3

struct drv_peri_ge_opt_reg_s
{
    uint8 intr_enable;
    uint8 reg;
    uint8 bit_ctl;
    uint8 reg_index;                             /**< ge phy have 2 optinal registers to set, indicate which index 0 or 1 */
};
typedef struct drv_peri_ge_opt_reg_s  drv_peri_ge_opt_reg_t;

struct drv_peri_xg_opt_reg_s
{
    uint8 intr_enable;
    uint8 dev;
    uint16 reg;
    uint8 bit_ctl;
    uint8 rsv[3];
};
typedef struct drv_peri_xg_opt_reg_s drv_peri_xg_opt_reg_t;

struct drv_peri_phy_scan_ctrl_s
{
    uint32 op_flag;
    uint32 scan_interval;
    uint32 mdio_use_phy0;
    uint32 mdio_use_phy1;
    uint32 scan_gephy_bitmap0;
    uint32 scan_gephy_bitmap1;
    uint8  scan_xgphy_bitmap0;
    uint8  scan_xgphy_bitmap1;
    uint8  xgphy_link_bitmask;
    uint8  xgphy_scan_twice;
};
typedef struct drv_peri_phy_scan_ctrl_s drv_peri_phy_scan_ctrl_t;

enum drv_peri_phy_scan_type_e
{
    DRV_PERI_INTERVAL_OP = 0x01,
    DRV_PERI_USE_PHY_OP = 0x02,
    DRV_PERI_GE_BITMAP_OP = 0x04,
    DRV_PERI_XG_BITMAP_OP = 0x08,
    DRV_PERI_XG_LINK_MASK_OP = 0x10,
    DRV_PERI_XG_TWICE_OP = 0x20
};
typedef enum drv_peri_phy_scan_type_e drv_peri_phy_scan_type_t;

/*2. I2c master struct */
enum drv_peri_sfp_scan_op_e
{
    DRV_PERI_SFP_INTERVAL_OP = 0x01,
    DRV_PERI_SFP_BITMAP_OP = 0x02,
    DRV_PERI_SFP_SCAN_REG_OP = 0x04
};
typedef enum drv_peri_sfp_scan_op_e drv_peri_sfp_scan_op_t;

struct drv_peri_i2c_read_s
{
    uint32 slave_bitmap;
    uint16 dev_addr;
    uint8  offset;
    uint8  length;
    uint8* buf;
};
typedef struct drv_peri_i2c_read_s drv_peri_i2c_read_t;

struct drv_peri_i2c_scan_s
{
    uint32 op_flag;
    uint32 slave_bitmap;
    uint16 dev_addr;
    uint8   offset;
    uint8   length;
    uint32  interval;
};
typedef struct drv_peri_i2c_scan_s drv_peri_i2c_scan_t;

struct drv_peri_i2c_write_s
{
    uint16 dev_addr;
    uint8  offset;
    uint8  slave_id;
    uint8  data;
    uint8  rsv;
};
typedef struct drv_peri_i2c_write_s drv_peri_i2c_write_t;

/* mac led data */
enum drv_peri_mac_led_type_e
{
    DRV_PERI_USING_ONE_LED,
    DRV_PERI_USING_TWO_LED,

    DRV_PERI_MAX_LED_TYPE
};
typedef enum drv_peri_mac_led_type_e drv_peri_mac_led_type_t;

enum drv_peri_led_mode_e
{
    DRV_PERI_RXLINK_MODE,     /**<  led on when rx link up, led off when rx link down, not blind */
    DRV_PERI_TXLINK_MODE,     /**<  led on when tx link up, led off when tx link down, not blind, only for 10g mac */
    DRV_PERI_RXLINK_RXACTIVITY_MODE,     /**<  led on when rx link up, led off when rx link down, led blind when rx activity */
    DRV_PERI_TXLINK_TXACTIVITY_MODE,     /**<  led on when tx link up, led off when tx link down, led blind when tx activity, only for 10g */
    DRV_PERI_RXLINK_BIACTIVITY_MODE,     /**<  led on when rx link up, off when rx link down, led blind when rx or tx activity */
    DRV_PERI_TXACTIVITY_MODE,                 /**<  led always off, led blind when tx activity */
    DRV_PERI_RXACTIVITY_MODE,                 /**<  led always off, led blind when rx activity */
    DRV_PERI_BIACTIVITY_MODE,                 /**<  led always off, led blind when rx or tx activity */
    DRV_PERI_FORCE_ON_MODE,                   /**<  led always on, not blind  */
    DRV_PERI_FORCE_OFF_MODE,                 /**<  led always off, not blind  */
    DRV_PERI_FORCE_ON_TXACTIVITY_MODE,       /**<  led always on, led blind when tx activity  */
    DRV_PERI_FORCE_ON_RXACTIVITY_MODE,       /**<  led always on, led blind when rx activity  */
    DRV_PERI_FORCE_ON_BIACTIVITY_MODE,       /**<  led always on, led blind when rx or tx activity  */

    DRV_PERI_MAC_LED_MODE
};
typedef enum drv_peri_led_mode_e drv_peri_led_mode_t;

struct drv_peri_led_para_s
{
    uint32 op_flag;
    uint16 port_id;                    /**< port id */
    uint8 rsv[2];
    uint32 polarity;                  /**< config led driver polarity */
    drv_peri_led_mode_t  first_mode;               /**< first mac led mode */
    drv_peri_led_mode_t  sec_mode;                /**< second mac led mode, only use when using two leds */
};
typedef struct drv_peri_led_para_s drv_peri_led_para_t;

enum drv_peri_mac_led_op_e
{
    DRV_PERI_LED_MODE_SET_OP = 0x01,
    DRV_PERI_LED_POLARITY_SET_OP = 0x02
};
typedef enum drv_peri_mac_led_op_e drv_peri_mac_led_op_t;

struct drv_peri_mac_led_mapping_s
{
    uint8* p_mac_id;
    uint8 mac_led_num;
};
typedef struct drv_peri_mac_led_mapping_s drv_peri_mac_led_mapping_t;

/* GPIO data */
enum drv_peri_gpio_mode_e
{
    DRV_PERI_INPUT_MODE,
    DRV_PERI_OUTPUT_MODE,
    DRV_PERI_SPECIAL_MODE,

    DRV_PERI_MAX_GPIO_MODE
};
typedef enum drv_peri_gpio_mode_e drv_peri_gpio_mode_t;

extern int32
drv_greatbelt_peri_init(uint8 lchip);

extern int32
drv_greatbelt_chip_peri_mutex_init(uint8 chip_id_offset);

/**
 @brief mdio interface for read ge phy
*/
extern int32
drv_greatbelt_peri_read_gephy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 reg, uint16* p_value);
/**
 @brief phy auto scan parameter
*/
extern int32
drv_greatbelt_peri_set_phy_scan_para(uint8 lchip, drv_peri_phy_scan_ctrl_t* p_scan_ctrl);
/**
 @brief phy auto scan enable
*/
extern int32
drv_greatbelt_peri_set_phy_scan_en(uint8 lchip, bool enable);
/**
 @brief mdio interface for write ge phy
*/
extern int32
drv_greatbelt_peri_write_gephy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 reg, uint16 value);

/**
 @brief mdio interface for read xg phy
*/
extern int32
drv_greatbelt_peri_read_xgphy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16* p_value);

/**
 @brief mdio interface for write xg phy
*/
extern int32
drv_greatbelt_peri_write_xgphy_reg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16 value);

/**
 @brief mdio interface for special:read xg phy by 1g mdio bus
*/
extern int32
drv_greatbelt_peri_read_gephy_xgreg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16* p_value);
/**
 @brief mdio interface for special:write xg phy by 1g mdio bus
*/
extern int32
drv_greatbelt_peri_write_gephy_xgreg(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint8 dev, uint16 reg, uint16 value);
/**
 @brief smi interface for set auto scan ge phy optional reg
*/
extern int32
drv_greatbelt_peri_set_gephy_scan_special_reg(uint8 lchip, drv_peri_ge_opt_reg_t* p_gephy_opt, bool enable);
/**
 @brief smi interface for set auto scan xg phy optional reg
*/
extern int32
drv_greatbelt_peri_set_xgphy_scan_special_reg(uint8 lchip, drv_peri_xg_opt_reg_t* p_xgphy_opt, bool enable);
/**
 @brief i2c master for read sfp
*/
extern int32
drv_greatbelt_peri_i2c_read(uint8 lchip, drv_peri_i2c_read_t* p_read_para);
/**
 @brief set i2c polling read para
*/
extern int32
drv_greatbelt_peri_set_i2c_scan_para(uint8 lchip, drv_peri_i2c_scan_t* p_sfp_scan);
/**
 @brief i2c master polling read start or stop
*/
extern int32
drv_greatbelt_peri_set_i2c_scan_en(uint8 lchip, bool enable);
/**
 @brief i2c master read databuffer ,the interface is used usually for polling read result
*/
extern int32
drv_greatbelt_peri_read_i2c_buf(uint8 lchip, uint32 len, uint8* p_buffer);
/**
 @brief i2c master for write sfp
*/
extern int32
drv_greatbelt_peri_i2c_write(uint8 lchip, drv_peri_i2c_write_t* p_sfp_para);
/**
 @brief set mac and led mapping
*/
int32
drv_greatbelt_peri_set_mac_led_mapping(uint8 lchip, drv_peri_mac_led_mapping_t* p_led_mapping);
/**
 @brief mac led interface
*/
extern int32
drv_greatbelt_peri_set_mac_led_mode(uint8 lchip, drv_peri_led_para_t* p_led_para, drv_peri_mac_led_type_t led_type);
/**
 @brief set mac led enable
*/
extern int32
drv_greatbelt_peri_set_mac_led_en(uint8 lchip, bool enable);
/**
 @brief gpio interface
*/
extern int32
drv_greatbelt_peri_set_gpio_mode(uint8 lchip, uint8 gpio_id, drv_peri_gpio_mode_t gpio_mode);

extern int32
drv_greatbelt_peri_set_gpio_output(uint8 lchip, uint8 gpio, uint8 out_para);

extern int32
drv_greatbelt_peri_get_gpio_input(uint8 lchip, uint8 gpio, uint8* in_value);

#endif

