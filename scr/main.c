/*
   Copyright 2023 Achim Pieters | StudioPieters®

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_common.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <FreeRTOS.h>
#include <task.h>
#include <esplibs/libmain.h>
#include <queue.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <wifi_config.h>

#include <button.h>
#include "software_versioning.h"

#define LED_SERVICE_GPIO         4  // this is the red LED to show boot, indentyfy and pdate status
bool service_led_on = false;
#define RELAY_GPIO              12  // this is the pin to turn on/off the relay
#define BUTTON_GPIO             14 // this is the pin to control the button

void relay_state_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context);
void button_callback(button_event_t event, void* context);

void outlet_state(bool on) {
        gpio_write(RELAY_GPIO, on ? 1 : 0);
}

void led_service_write(bool on) {
        gpio_write(LED_SERVICE_GPIO, on ? 0 : 1);
}

void led_init() {
        gpio_enable(LED_SERVICE_GPIO, GPIO_OUTPUT);
        led_service_write(service_led_on);
}

homekit_characteristic_t relay_state = HOMEKIT_CHARACTERISTIC_(
        ON, false, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(relay_state_callback)
        );

void gpio_init() {
        gpio_enable(LED_SERVICE_GPIO, GPIO_OUTPUT);
        led_service_write(false);
        gpio_enable(RELAY_GPIO, GPIO_OUTPUT);
        outlet_state(relay_state.value.bool_value);
}

void relay_state_callback(homekit_characteristic_t *_ch, homekit_value_t on, void *context) {
        outlet_state(relay_state.value.bool_value);
        led_service_write(relay_state.value.bool_value);
}

void reset_configuration_task() {
//Flash the LED first before we start the reset
        for (int i=0; i<3; i++) {
                led_service_write(true);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                led_service_write(false);
                vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        printf("Resetting Wifi Config\n");
        wifi_config_reset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf("Resetting HomeKit Config\n");
        homekit_server_reset();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        printf("Restarting\n");
        sdk_system_restart();
        vTaskDelete(NULL);
}

void reset_configuration() {
        printf("Resetting Smart plug configuration\n");
        xTaskCreate(reset_configuration_task, "Reset Smart plug", 256, NULL, 2, NULL);
}

void button_callback(button_event_t event, void* context)  {
        switch (event) {
        case button_event_single_press:
                printf("Toggling relay\n");
                relay_state.value.bool_value = !relay_state.value.bool_value;
                outlet_state(relay_state.value.bool_value);
                homekit_characteristic_notify(&relay_state, relay_state.value);
                break;
        case button_event_double_press:
                printf("double press\n");
                break;
        case button_event_tripple_press:
                printf("tripple press\n");
                break;
        case button_event_long_press:
                printf("long press\n");
                reset_configuration();
                break;
        default:
                printf("Unknown button event: %d\n", event);
        }
}

void led_identify_task(void *_args) {
        for (int i=0; i<3; i++) {
                for (int j=0; j<2; j++) {
                        led_service_write(true);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        led_service_write(false);
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                vTaskDelay(250 / portTICK_PERIOD_MS);
        }
        led_service_write(service_led_on);
        vTaskDelete(NULL);
}

void identify(homekit_value_t _value) {
        printf("Smart plug identify\n");
        xTaskCreate(led_identify_task, "Smart plug identify", 128, NULL, 2, NULL);
}

#define DEVICE_NAME "Smart plug"
#define DEVICE_MANUFACTURER "StudioPieters®"
// See Naming convention.md
#define DEVICE_SERIAL "NLDA4SQN1466"
// See Naming convention.md
#define DEVICE_MODEL "SD466NL/A"
// Will get his version numer trough software_versioning
#define FW_VERSION "0.0.0"

homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model= HOMEKIT_CHARACTERISTIC_(MODEL, DEVICE_MODEL);
homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

homekit_accessory_t *accessories[] = {
        HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_outlet, .services=(homekit_service_t*[]){
                HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
                        &name,
                        &manufacturer,
                        &serial,
                        &model,
                        &revision,
                        HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),
                        NULL
                }),
                HOMEKIT_SERVICE(OUTLET, .primary=true, .characteristics=(homekit_characteristic_t*[]){
                        HOMEKIT_CHARACTERISTIC(NAME, "Smart plug"),
                        &relay_state,
                        &ota_trigger,
                        NULL
                }),
                NULL
        }),
        NULL
};

// tools/gen_qrcode 7 772-55-929 L7SC qrcode.png
homekit_server_config_t config = {
        .accessories = accessories,
        .password = "772-55-929",
        .setupId="L7SC",
};

void create_accessory_name() {
        int serialLength = snprintf(NULL, 0, "%d", sdk_system_get_chip_id());
        char *serialNumberValue = malloc(serialLength + 1);
        snprintf(serialNumberValue, serialLength + 1, "%d", sdk_system_get_chip_id());
        int name_len = snprintf(NULL, 0, "%s-%s", DEVICE_NAME, serialNumberValue);
        if (name_len > 63) { name_len = 63; }
        char *name_value = malloc(name_len + 1);
        snprintf(name_value, name_len + 1, "%s-%s", DEVICE_NAME, serialNumberValue);
        name.value = HOMEKIT_STRING(name_value);
}

void on_wifi_ready() {
}

void user_init(void) {
        uart_set_baud(0, 115200);
        create_accessory_name();
        homekit_server_init(&config);
        led_init();
        gpio_init();

        // Get Github version number
        int c_hash=ota_read_sysparam(&revision.value.string_value);
        config.accessories[0]->config_number=c_hash;

        button_config_t config = BUTTON_CONFIG(
                button_active_low,
                .long_press_time = 4000,
                .max_repeat_presses = 3,
                );

                int b = button_create(BUTTON_GPIO, config, button_callback, NULL);
                if (b) {
                        printf("Failed to initialize a button\n");
                }


}
