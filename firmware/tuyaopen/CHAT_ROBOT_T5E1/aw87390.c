/**
 * @file aw87390.c
 * @brief Minimal AW87390FCR speaker-mode driver for the Chat Robot PCB.
 *
 * Address and register values come from the AW87390FCR V1.3 datasheet. AD1
 * and AD2 are tied low on the PCB, so the 7-bit address is 0x58.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_i2c.h"
#include "tkl_pinmux.h"

#include "aw87390.h"

/* Port 2 has no hardware pin group, so the adapter bit-bangs the pins
 * selected below. Hardware I2C1 cannot be mapped to GPIO38/39. */
#define AW87390_I2C_PORT       TUYA_I2C_NUM_2
#define AW87390_SCL_PIN       TUYA_GPIO_NUM_38
#define AW87390_SDA_PIN       TUYA_GPIO_NUM_39
#define AW87390_I2C_ADDR      0x58

#define AW87390_REG_ID         0x00
#define AW87390_REG_SYSCTRL    0x01
#define AW87390_REG_MODECTRL   0x02
#define AW87390_REG_GAIN       0x05

#define AW87390_ID             0x76
#define AW87390_SOFT_RESET     0xAA
/* AW87390FCR V1.3: 0x02 = 0x0C selects speaker mode. SYSCTRL bit 0 is
 * the software power-up. 0x00 powers the part down. */
#define AW87390_SYSCTRL_DOWN   0x00
/* bit0 software enable, plus PA, charge pump, speaker and 2x boost.
 * bit0 alone leaves SP+/SP- at 0 V. */
#define AW87390_SYSCTRL_UP     0x1F
#define AW87390_MODE_SPEAKER   0x0C
#define AW87390_GAIN_18DB      0x0C

static OPERATE_RET aw87390_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    OPERATE_RET rt = tkl_i2c_master_send(AW87390_I2C_PORT, AW87390_I2C_ADDR, buf,
                                         sizeof(buf), TRUE);
    if (OPRT_OK != rt) {
        PR_ERR("AW87390 write reg 0x%02x failed: %d", reg, rt);
    }
    return rt;
}

static OPERATE_RET aw87390_read(uint8_t reg, uint8_t *value)
{
    OPERATE_RET rt;

    rt = tkl_i2c_master_send(AW87390_I2C_PORT, AW87390_I2C_ADDR, &reg, 1,
                             FALSE);
    if (OPRT_OK != rt) {
        return rt;
    }

    return tkl_i2c_master_receive(AW87390_I2C_PORT, AW87390_I2C_ADDR, value,
                                  1, TRUE);
}

static uint8_t aw87390_gain_code(uint8_t gain_db)
{
    /* PAG register: 12/15/18/21/24 dB map to codes 0..4. */
    if (gain_db < 12 || gain_db > 24 || ((gain_db - 12) % 3) != 0) {
        return 0xFF;
    }

    return (uint8_t)((gain_db - 12) / 3);
}

OPERATE_RET chat_robot_aw87390_init(void)
{
    OPERATE_RET rt;
    uint8_t id = 0xFF;
    TUYA_IIC_BASE_CFG_T cfg = {0};

    /* Software I2C on the schematic pins. Hardware I2C1 only accepts
     * GPIO0/1 or GPIO42/43, so mapping 38/39 there never reaches the amp. */
    TUYA_CALL_ERR_RETURN(tkl_io_pinmux_config(AW87390_SCL_PIN, TUYA_IIC2_SCL));
    TUYA_CALL_ERR_RETURN(tkl_io_pinmux_config(AW87390_SDA_PIN, TUYA_IIC2_SDA));

    cfg.role = TUYA_IIC_MODE_MASTER;
    cfg.speed = TUYA_IIC_BUS_SPEED_100K;
    cfg.addr_width = TUYA_IIC_ADDRESS_7BIT;
    TUYA_CALL_ERR_RETURN(tkl_i2c_init(AW87390_I2C_PORT, &cfg));
    PR_NOTICE("AW87390 software I2C on GPIO38/GPIO39, addr 0x%02x",
              AW87390_I2C_ADDR);

    /* VDD and the charge-pump capacitors must settle first. */
    tal_system_sleep(20);

    TUYA_CALL_ERR_RETURN(aw87390_read(AW87390_REG_ID, &id));
    if (AW87390_ID != id) {
        PR_ERR("AW87390 ID mismatch: got 0x%02x, expected 0x%02x", id,
               AW87390_ID);
        return OPRT_COM_ERROR;
    }

    /* Soft reset, select speaker mode, then power up last. The charge
     * pump needs time after SYSCTRL bit 0 before the speaker can swing. */
    TUYA_CALL_ERR_RETURN(aw87390_write(AW87390_REG_ID, AW87390_SOFT_RESET));
    tal_system_sleep(5);
    TUYA_CALL_ERR_RETURN(aw87390_write(AW87390_REG_SYSCTRL,
                                       AW87390_SYSCTRL_DOWN));
    TUYA_CALL_ERR_RETURN(aw87390_write(AW87390_REG_MODECTRL,
                                       AW87390_MODE_SPEAKER));
    TUYA_CALL_ERR_RETURN(aw87390_write(AW87390_REG_GAIN, AW87390_GAIN_18DB));
    TUYA_CALL_ERR_RETURN(aw87390_write(AW87390_REG_SYSCTRL,
                                       AW87390_SYSCTRL_UP));
    tal_system_sleep(20);
    PR_NOTICE("AW87390 speaker mode 0x0C, SYSCTRL 0x1F, id 0x%02x", id);
    return OPRT_OK;
}

OPERATE_RET chat_robot_aw87390_set_gain_db(uint8_t gain_db)
{
    uint8_t code = aw87390_gain_code(gain_db);
    if (0xFF == code) {
        return OPRT_INVALID_PARM;
    }

    return aw87390_write(AW87390_REG_GAIN, code);
}

OPERATE_RET chat_robot_aw87390_set_mute(bool mute)
{
    return aw87390_write(AW87390_REG_SYSCTRL,
                         mute ? AW87390_SYSCTRL_DOWN : AW87390_SYSCTRL_UP);
}
