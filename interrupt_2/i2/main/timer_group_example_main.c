/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/timer.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"

#include "esp_intr_alloc.h"
#include "esp_log.h"

#define TIMER_RESOLUTION_HZ   1000000 // 1MHz resolution
#define TIMER_ALARM_PERIOD_S_1  2.90   // Alarm period 0.5s
#define TIMER_ALARM_PERIOD_S_2  3    // Alarm period 0.5s
#define GPIO_OUTPUT_IO_0    8

#define GPIO_OUTPUT_IO_1    9

#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))
static const char *TAG = "example";

/**
 * @brief A sample structure to pass events from the timer ISR to task
 */
typedef struct {
    uint64_t timer_count_value;
} example_timer_event_t;

/**
 * @brief Timer user data, will be pass to timer alarm callback
 */
typedef struct {
    int timer_group;
    int timer_idx;
    int alarm_value;
    bool auto_reload;
} example_timer_user_data_t;
char * value_event_1="e1";

char * value_event_2="e2";
int value=0;
static void IRAM_ATTR timer_group_isr_callback_1(void *args)
{

    gpio_set_level(GPIO_OUTPUT_IO_0, 0);
    for (int i=0; i <3000000; i++) {

        value+=1;
    }
    if (value!=2 && value!=4) {
        value=1;
    }
    else {
        value=3;
    }
    gpio_set_level(GPIO_OUTPUT_IO_0, 1);
}
static void IRAM_ATTR timer_group_isr_callback_2(void *args)
{

    gpio_set_level(GPIO_OUTPUT_IO_1, 0);
    for (int i=0; i <3000000; i++) {

        value+=1;
    }
    value=2;
    if (value==3) {
        value=4;
    } 
    gpio_set_level(GPIO_OUTPUT_IO_1, 1);
}
static void example_tg_timer_init_1(example_timer_user_data_t *user_data)
{
    int group = user_data->timer_group;
    int timer = user_data->timer_idx;

    timer_config_t config = {
        .clk_src = TIMER_SRC_CLK_APB,
        .divider = APB_CLK_FREQ / TIMER_RESOLUTION_HZ,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = user_data->auto_reload,
    };
    ESP_ERROR_CHECK(timer_init(group, timer, &config));

    // For the timer counter to a initial value
    ESP_ERROR_CHECK(timer_set_counter_value(group, timer, 0));
    // Set alarm value and enable alarm interrupt
    ESP_ERROR_CHECK(timer_set_alarm_value(group, timer, user_data->alarm_value));
    ESP_ERROR_CHECK(timer_enable_intr(group, timer));
    // Hook interrupt callback
    //!!!!!!!!!
    ESP_ERROR_CHECK(timer_isr_callback_add(group, timer, timer_group_isr_callback_1, user_data, ESP_INTR_FLAG_IRAM));
    // Start timer
    ESP_ERROR_CHECK(timer_start(group, timer));
}
static void example_tg_timer_init_2(example_timer_user_data_t *user_data)
{
    int group = user_data->timer_group;
    int timer = user_data->timer_idx;

    timer_config_t config = {
        .clk_src = TIMER_SRC_CLK_APB,
        .divider = APB_CLK_FREQ / TIMER_RESOLUTION_HZ,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN,
        .auto_reload = user_data->auto_reload,
    };
    ESP_ERROR_CHECK(timer_init(group, timer, &config));

    // For the timer counter to a initial value
    ESP_ERROR_CHECK(timer_set_counter_value(group, timer, 0));
    // Set alarm value and enable alarm interrupt
    ESP_ERROR_CHECK(timer_set_alarm_value(group, timer, user_data->alarm_value));
    ESP_ERROR_CHECK(timer_enable_intr(group, timer));
    // Hook interrupt callback
    //!!!!!!!!!
    ESP_ERROR_CHECK(timer_isr_callback_add(group, timer, timer_group_isr_callback_2, user_data, ESP_INTR_FLAG_LEVEL2|ESP_INTR_FLAG_IRAM|ESP_INTR_FLAG_LOWMED));
    // Start timer
    ESP_ERROR_CHECK(timer_start(group, timer));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Init timer with auto-reload");
    example_timer_user_data_t *user_data_1 = calloc(1, sizeof(example_timer_user_data_t));
    assert(user_data_1);
    user_data_1->timer_group = TIMER_GROUP_1;
    user_data_1->timer_idx = 0;
    user_data_1->alarm_value = TIMER_ALARM_PERIOD_S_1 * TIMER_RESOLUTION_HZ;
   //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    
    gpio_set_level(GPIO_OUTPUT_IO_0, 1);

    gpio_set_level(GPIO_OUTPUT_IO_1, 1);
    user_data_1->auto_reload = true;
    example_tg_timer_init_1(user_data_1);
    example_timer_user_data_t *user_data_2 = calloc(1, sizeof(example_timer_user_data_t));
    assert(user_data_2);
    user_data_2->timer_group = TIMER_GROUP_0;
    user_data_2->timer_idx = 0;
    user_data_2->alarm_value = TIMER_ALARM_PERIOD_S_2 * TIMER_RESOLUTION_HZ;


    ESP_LOGI(TAG, "Init timer with auto-reload");
    user_data_2->auto_reload = true;
    example_tg_timer_init_2(user_data_2);
    free(user_data_1);

    free(user_data_2);

}
