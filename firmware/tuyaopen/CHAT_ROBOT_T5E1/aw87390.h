#ifndef CHAT_ROBOT_AW87390_H
#define CHAT_ROBOT_AW87390_H

#include "tuya_cloud_types.h"

OPERATE_RET chat_robot_aw87390_init(void);
OPERATE_RET chat_robot_aw87390_set_gain_db(uint8_t gain_db);
OPERATE_RET chat_robot_aw87390_set_mute(bool mute);

/* Write a full-scale 1 kHz square wave straight into the T5 DAC FIFO. */
void chat_robot_dac_tone_loop(void);

#endif
