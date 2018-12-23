/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#ifndef __APP_MAIN_H__
#define __APP_MAIN_H__

#include "cdnet_dispatch.h"
#include "cdbus_uart.h"
#include "cdctl_it.h"

#define APP_CONF_ADDR       0x0801F800 // last page

typedef struct {
    uint16_t        magic_code; // 0xcdcd
    bool            bl_wait; // run app after timeout (unit 0.1s), 0xff: never

    uint8_t         rs485_net;
    uint8_t         rs485_mac;
    uint32_t        rs485_baudrate_low;
    uint32_t        rs485_baudrate_high;

    bool            dbg_en;
    cd_sockaddr_t   dbg_dst;
} app_conf_t;

typedef enum {
    ST_STOP = 0,
    ST_CALIBRATION,
    ST_CONST_CURRENT,
    ST_CONST_SPEED,
    ST_POSITION
} state_t;

typedef enum {
    LED_POWERON = 0,
    LED_WARN,
    LED_ERROR
} led_state_t;


extern app_conf_t app_conf;

void app_main(void);
void load_conf_early(void);
void load_conf(void);
void save_conf(void);
void common_service_init(void);
void common_service_routine(void);
void debug_init(bool *en, cd_sockaddr_t *dst);

void set_led_state(led_state_t state);

void app_motor(void);
void app_motor_init(void);
void limit_det_isr(void);

#endif
