/*
 * Software License Agreement (BSD License)
 *
 * Copyright (c) 2017, DUKELEC, Inc.
 * All rights reserved.
 *
 * Author: Duke Fong <duke@dukelec.com>
 */

#include "app_main.h"

extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc2;
extern ADC_HandleTypeDef hadc3;
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;
extern UART_HandleTypeDef huart1;
extern TIM_HandleTypeDef htim1;

static gpio_t led_r = { .group = LED_RED_GPIO_Port, .num = LED_RED_Pin };
static gpio_t led_g = { .group = LED_GRN_GPIO_Port, .num = LED_GRN_Pin };

static gpio_t drv_a_n = { .group = DRV_A_N_GPIO_Port, .num = DRV_A_N_Pin };
static gpio_t drv_b_n = { .group = DRV_B_N_GPIO_Port, .num = DRV_B_N_Pin };
static gpio_t drv_c_n = { .group = DRV_C_N_GPIO_Port, .num = DRV_C_N_Pin };

static gpio_t drv_en = { .group = DRV_EN_GPIO_Port, .num = DRV_EN_Pin };
static gpio_t drv_nss = { .group = DRV_NSS_GPIO_Port, .num = DRV_NSS_Pin };
static gpio_t s_nss = { .group = S_NSS_GPIO_Port, .num = S_NSS_Pin };

uart_t debug_uart = { .huart = &huart1 };

static gpio_t r_rst_n = { .group = CDCTL_RST_N_GPIO_Port, .num = CDCTL_RST_N_Pin };
static gpio_t r_int_n = { .group = CDCTL_INT_N_GPIO_Port, .num = CDCTL_INT_N_Pin };
static gpio_t r_ns = { .group = CDCTL_NSS_GPIO_Port, .num = CDCTL_NSS_Pin };
static spi_t r_spi = { .hspi = &hspi1, .ns_pin = &r_ns };

#define FRAME_MAX 10
static cd_frame_t frame_alloc[FRAME_MAX];
static list_head_t frame_free_head = {0};

#define PACKET_MAX 10
static cdnet_packet_t packet_alloc[PACKET_MAX];

static cdctl_dev_t r_dev = {0};    // RS485
static cdnet_intf_t n_intf = {0};  // CDNET


static void device_init(void)
{
    int i;
    for (i = 0; i < FRAME_MAX; i++)
        list_put(&frame_free_head, &frame_alloc[i].node);
    for (i = 0; i < PACKET_MAX; i++)
        list_put(&cdnet_free_pkts, &packet_alloc[i].node);

    cdctl_dev_init(&r_dev, &frame_free_head, app_conf.rs485_mac,
            app_conf.rs485_baudrate_low, app_conf.rs485_baudrate_high,
            &r_spi, &r_rst_n, &r_int_n);
    cdnet_intf_init(&n_intf, &r_dev.cd_dev, app_conf.rs485_net, app_conf.rs485_mac);
    cdnet_intf_register(&n_intf);
}

void set_led_state(led_state_t state)
{
    static bool is_err = false;
    if (is_err)
        return;

    switch (state) {
    case LED_POWERON:
        gpio_set_value(&led_r, 0);
        gpio_set_value(&led_g, 1);
        break;
    case LED_WARN:
        gpio_set_value(&led_r, 1);
        gpio_set_value(&led_g, 0);
        break;
    default:
    case LED_ERROR:
        is_err = true;
        gpio_set_value(&led_r, 1);
        gpio_set_value(&led_g, 1);
        break;
    }
}

#ifdef BOOTLOADER
#define APP_ADDR 0x08010000 // offset: 64KB

static void jump_to_app(void)
{
    uint32_t stack = *(uint32_t*)APP_ADDR;
    uint32_t func = *(uint32_t*)(APP_ADDR + 4);
    printf("jump to app...\n");
    __set_MSP(stack); // init stack pointer
    ((void(*)()) func)();
}
#endif


static int sensor_read(void)
{
    uint16_t buf[2];
    int ret = 0;

    buf[0] = 0x8021;

    gpio_set_value(&s_nss, 0);
    ret = HAL_SPI_Transmit(&hspi2, (uint8_t *)buf, 1, HAL_MAX_DELAY);

    GPIOC->MODER &= ~(1 << (3 * 2 + 1));
    ret = HAL_SPI_Receive(&hspi2, (uint8_t *)buf, 2, HAL_MAX_DELAY);
    gpio_set_value(&s_nss, 1);
    GPIOC->MODER |= 1 << (3 * 2 + 1);

    d_debug("%04x %04x\n", buf[0], buf[1]);

    return ret;
}

static uint16_t drv_read_reg(uint8_t reg)
{
    uint16_t rx_val;
    uint16_t val = 0x8000 | reg << 11;

    gpio_set_value(&drv_nss, 0);
    HAL_SPI_TransmitReceive(&hspi3, (uint8_t *)&val, (uint8_t *)&rx_val, 1, HAL_MAX_DELAY);
    gpio_set_value(&drv_nss, 1);
    return rx_val & 0x7ff;
}

static void drv_write_reg(uint8_t reg, uint16_t val)
{
    val |= reg << 11;

    gpio_set_value(&drv_nss, 0);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)&val, 1, HAL_MAX_DELAY);
    gpio_set_value(&drv_nss, 1);
}


void app_main(void)
{
#ifdef BOOTLOADER
    printf("\nstart app_main (bl_wait: %d)...\n", app_conf.bl_wait);
#else
    printf("\nstart app_main...\n");
#endif
    debug_init(&app_conf.dbg_en, &app_conf.dbg_dst);
    load_conf();
    device_init();
    common_service_init();
    app_motor_init();

    gpio_set_value(&drv_en, 1);
    delay_systick(50);
    d_debug("drv 02: %04x\n", drv_read_reg(0x02));
    drv_write_reg(0x02, drv_read_reg(0x02) | 0x1 << 5);
    d_debug("drv 02: %04x\n", drv_read_reg(0x02));

    HAL_ADC_Start(&hadc1);
    HAL_ADC_Start(&hadc2);
    HAL_ADC_Start(&hadc3);
    HAL_ADCEx_InjectedStart_IT(&hadc1);

    d_info("start pwm...\n");
    HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim1,TIM_CHANNEL_4);

    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 100);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 500);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 550);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 100);
    d_info("pwm on.\n");
    set_led_state(LED_POWERON);
    //delay_systick(500);
    //__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 4095);
    //__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, 4095);
    //__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, 4095);
    //while(1);

#ifdef BOOTLOADER
    uint32_t boot_time = get_systick();
#endif

    while (true) {
#ifdef BOOTLOADER
        if (app_conf.bl_wait != 0xff &&
                get_systick() - boot_time > app_conf.bl_wait * 100000 / SYSTICK_US_DIV)
            jump_to_app();
#endif

        //sensor_read();
        //d_debug("drv: %08x\n", drv_read_reg(0x01) << 16 | drv_read_reg(0x00));


        cdnet_intf_routine(); // handle cdnet
        common_service_routine();
        app_motor();
        debug_flush();
    }
}




void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    uint32_t v1 = HAL_ADCEx_InjectedGetValue(&hadc1, 1);
    uint32_t v2 = HAL_ADCEx_InjectedGetValue(&hadc2, 1);
    uint32_t v3 = HAL_ADCEx_InjectedGetValue(&hadc3, 1);
    //d_debug("@%p %d %d %d\n", hadc, v1, v2, v3);

    //HAL_ADCEx_InjectedStart_IT(&hadc1);
    //HAL_ADCEx_InjectedStart_IT(&hadc2);
    //HAL_ADCEx_InjectedStart_IT(&hadc3);
    gpio_set_value(&led_r, !gpio_get_value(&led_r));
}


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == r_int_n.num) {
        cdctl_int_isr(&r_dev);
    }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    cdctl_spi_isr(&r_dev);
}
void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
    cdctl_spi_isr(&r_dev);
}
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    cdctl_spi_isr(&r_dev);
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    d_error("spi error...\n");
}
