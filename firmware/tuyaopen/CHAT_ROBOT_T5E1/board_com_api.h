/**
 * @file board_com_api.h
 * @brief Board registration entry used by TuyaOpen applications.
 */

#ifndef __BOARD_COM_API_H__
#define __BOARD_COM_API_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET board_register_hardware(void);

#ifdef __cplusplus
}
#endif

#endif
