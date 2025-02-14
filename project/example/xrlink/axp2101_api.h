/*
 * Copyright (C) 2016 Allwinner.
 * wangwei <wangwei@allwinnertech.com>
 *
 * SUNXI AXP21  Driver
 *
 */

#ifndef __AXP2101_API_H__
#define __AXP2101_API_H__


/*PMIC chip id reg03:bit7-6  bit3-*/
#define   AXP2101_CHIP_ID             (0x47)
#define   AXP2101_CHIP_ID_B           (0x4a)

#define CFG_SUNXI_TWI 1

#define AXP2101_FREN			400000
#ifdef CFG_SUNXI_TWI
#define AXP2101_RUNTIME_ADDR			(0x34)
#else
#define AXP2101_RUNTIME_ADDR            (0x2d)
#endif
/* define AXP21 REGISTER */
#define   AXP2101_MODE_CHGSTATUS	(0x01)
#define   AXP2101_OTG_STATUS		(0x02)
#define   AXP2101_VERSION			(0x03)
#define   AXP2101_DATA_BUFFER0		(0x04)
#define   AXP2101_DATA_BUFFER1		(0x05)
#define   AXP2101_DATA_BUFFER2		(0x06)
#define   AXP2101_DATA_BUFFER3		(0x07)
#define   AXP2101_OUTPUT_CTL0			(0x80)
#define   AXP2101_OUTPUT_CTL1			(0x81)
#define   AXP2101_OUTPUT_CTL2			(0x90)
#define   AXP2101_OUTPUT_CTL3			(0x91)

#define   AXP2101_DC1OUT_VOL			(0x82)
#define   AXP2101_DC2OUT_VOL		(0x83)
#define   AXP2101_DC3OUT_VOL		(0x84)
#define   AXP2101_DC4OUT_VOL		(0x85)
#define   AXP2101_DC5OUT_VOL		(0x86)
#define   AXP2101_ALDO1OUT_VOL			(0x92)
#define   AXP2101_ALDO2OUT_VOL			(0x93)
#define   AXP2101_ALDO3OUT_VOL			(0x94)
#define   AXP2101_ALDO4OUT_VOL			(0x95)
#define   AXP2101_BLDO1OUT_VOL			(0x96)
#define   AXP2101_BLDO2OUT_VOL			(0x97)
#define   AXP2101_CPUSLDO_VOL				(0x98)
#define   AXP2101_DLDO1OUT_VOL			(0x99)
#define   AXP2101_DLDO2OUT_VOL			(0x9A)

#define   AXP2101_PWRON_STATUS			(0x20)
#define   AXP2101_PWEON_PWEOFF_EN		(0x22)
#define   AXP2101_DCDC_PWEOFF_EN		(0x23)
#define   AXP2101_VSYS_MIN			(0x14)
#define   AXP2101_VBUS_VOL_SET		(0x15)
#define   AXP2101_VBUS_CUR_SET			(0x16)
#define   AXP2101_OFF_CTL		(0x10)
#define   AXP2101_CHARGE1		(0x62)
#define   AXP2101_CHGLED_SET			(0x69)

#define   AXP2101_INTEN0		(0x40)
#define   AXP2101_INTEN1		(0x41)
#define   AXP2101_INTEN2		(0x42)

#define   AXP2101_INTSTS0		(0x48)
#define   AXP2101_INTSTS1		(0x49)
#define   AXP2101_INTSTS2		(0x4a)

#define   AXP2101_BAT_AVERVOL_H6          (0x34)
#define   AXP2101_BAT_AVERVOL_L8          (0x35)

#define   AXP2101_TWI_ADDR_EXT			(0xFF)
#define   AXP2101_EFUS_OP_CFG			(0xF0)
#define   AXP2101_EFREQ_CTRL			(0xF1)
#define   AXP2101_SELLP_CFG				(0x26)

int axp2101_probe_power_key(void);
int axp2101_ctrl_voltage(char *name, int set_vol, int onoff);
int axp2101_set_ddr_voltage(int set_vol, int onoff);
int axp2101_set_pll_voltage(int set_vol, int onoff);
int axp2101_set_efuse_voltage(int set_vol, int onoff);
int axp2101_set_sys_voltage(int set_vol, int onoff);
int axp2101_reg_read(uint8_t addr, uint8_t *val);
int axp2101_reg_write(uint8_t addr, uint8_t val);
int axp2101_axp_init(void);
int axp2101_set_vin_voltage(int onfof);
int axp2101_poweroff(void);
int axp2101_poweron(void);
int axp2101_get_poweron_source(void);

#endif /* __AXP2101_REGS_H__ */
