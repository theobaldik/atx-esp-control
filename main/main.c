// SPDX-License-Identifier: MIT

#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "ATX-ESP";

// Pin configurations from sdkconfig
static const gpio_num_t GPIO_MB_ON = (gpio_num_t)CONFIG_ATX_ESP_GPIO_MB_ON;
static const gpio_num_t GPIO_PWR_SW = (gpio_num_t)CONFIG_ATX_ESP_GPIO_PWR_SW;
static const gpio_num_t GPIO_PS_ON = (gpio_num_t)CONFIG_ATX_ESP_GPIO_PS_ON;
static const gpio_num_t GPIO_MB_ACPI = (gpio_num_t)CONFIG_ATX_ESP_GPIO_MB_ACPI;

#define ACPI_SIGNAL_MS 500
#define MB_RESTART_WAIT_MS 3000

static bool psu_last_state = false;
static bool mb_last_state = false;
static bool pwr_sw_last_state = false;

/**
 * Turns on or off the PSU by driving the PS_ON pin.
 */
esp_err_t psu_set_state(bool on)
{
    int8_t level = on ? 0 : 1; // PS_ON is active-low

    esp_err_t err;
    err = gpio_set_level(GPIO_PS_ON, level);
    if (err != ESP_OK)
        return err;

    psu_last_state = on;
    return err;
}

/**
 * Sends an ACPI power button signal to the motherboard by pulsing the MB_ACPI pin high for a short duration.
 */
esp_err_t mb_send_acpi_signal()
{
    esp_err_t err;
    err = gpio_set_level(GPIO_MB_ACPI, 1);
    if (err != ESP_OK)
        return err;

    vTaskDelay(pdMS_TO_TICKS(ACPI_SIGNAL_MS));

    err = gpio_set_level(GPIO_MB_ACPI, 0);
    if (err != ESP_OK)
        return err;

    return ESP_OK;
}

/**
 * Reads the current state of the PWR_SW pin.
 * Returns true if PWR_SW is pressed, false otherwise.
 */
bool pwr_sw_get_state()
{
    return !gpio_get_level(GPIO_PWR_SW); // PWR_SW is active-low
}

/**
 * Reads the current state of the MB_ON pin.
 * Returns true if MB_ON is high, false otherwise.
 */
bool mb_get_state()
{
    return gpio_get_level(GPIO_MB_ON);
}

/**
 * Configures the GPIO pins for PWR_SW, MB_ON, PS_ON, and MB_ACPI.
 */
esp_err_t configure_gpio()
{
    // PWR_SW
    gpio_config_t pwr_sw_config = {
        .pin_bit_mask = 1ULL << GPIO_PWR_SW,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    // MB_ON
    gpio_config_t mb_on_config = {
        .pin_bit_mask = 1ULL << GPIO_MB_ON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    // MB_ACPI
    gpio_config_t mb_acpi_config = {
        .pin_bit_mask = 1ULL << GPIO_MB_ACPI,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    // PS_ON
    gpio_config_t ps_on_config = {
        .pin_bit_mask = 1ULL << GPIO_PS_ON,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err;

    err = gpio_config(&pwr_sw_config);
    if (err != ESP_OK)
        return err;

    err = gpio_config(&mb_on_config);
    if (err != ESP_OK)
        return err;

    err = gpio_config(&ps_on_config);
    if (err != ESP_OK)
        return err;

    err = gpio_config(&mb_acpi_config);
    if (err != ESP_OK)
        return err;

    // Set default states
    err = psu_set_state(false);
    if (err != ESP_OK)
        return err;

    err = gpio_set_level(GPIO_MB_ACPI, 0);
    if (err != ESP_OK)
        return err;

    return ESP_OK;
}

// Main application entry point
void app_main(void)
{
    esp_err_t err = configure_gpio();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure GPIO: %d", err);
        return;
    }

    bool pwr_sw_state = pwr_sw_last_state = pwr_sw_get_state();
    bool mb_state = mb_last_state = mb_get_state();

    // Main event loop
    while (1)
    {
        mb_state = mb_get_state();
        pwr_sw_state = pwr_sw_get_state();

        // Button pressed event
        if (!pwr_sw_last_state && pwr_sw_state)
        {
            // PSU is off, turn it on
            if (!psu_last_state)
            {
                ESP_LOGI(TAG, "Turning PSU ON");
                err = psu_set_state(true);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to turn PSU ON: %d", err);
            }

            // PSU is on
            else
            {
                ESP_LOGI(TAG, "PSU is already ON");

                // MB is on, send ACPI signal to turn it off
                if (mb_state)
                {
                    ESP_LOGI(TAG, "Sending ACPI signal to MB");
                    err = mb_send_acpi_signal();
                    if (err != ESP_OK)
                        ESP_LOGE(TAG, "Failed to send ACPI signal: %d", err);
                }

                // MB is off, turn PSU off
                else
                {
                    ESP_LOGI(TAG, "Turning PSU OFF");
                    err = psu_set_state(false);
                    if (err != ESP_OK)
                        ESP_LOGE(TAG, "Failed to turn PSU OFF: %d", err);
                }
            }
        }

        // MB_ON went low event
        else if (mb_last_state && !mb_state)
        {
            // Detect if it's a shutdown or restart
            ESP_LOGI(TAG, "MB went OFF, waiting to see if it's a restart");
            vTaskDelay(pdMS_TO_TICKS(MB_RESTART_WAIT_MS));

            mb_state = gpio_get_level(GPIO_MB_ON);

            // Still off, assume shutdown
            if (!mb_state)
            {
                ESP_LOGI(TAG, "MB is still OFF, turning PSU OFF");
                err = psu_set_state(false);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "Failed to turn PSU OFF: %d", err);
            }

            // Back on, assume restart
            else
            {
                ESP_LOGI(TAG, "MB is back ON, assuming restart");
            }
        }

        // MB_ON went high event
        else if (!mb_last_state && mb_state)
        {
            ESP_LOGI(TAG, "MB is back ON");
        }

        // Update last states
        mb_last_state = mb_state;
        pwr_sw_last_state = pwr_sw_state;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
