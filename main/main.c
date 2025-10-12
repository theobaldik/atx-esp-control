// SPDX-License-Identifier: MIT

#include "sdkconfig.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Pin configuration from sdkconfig
static const gpio_num_t GPIO_MB_ON = (gpio_num_t)CONFIG_ATX_ESP_GPIO_MB_ON;
static const gpio_num_t GPIO_PWR_SW = (gpio_num_t)CONFIG_ATX_ESP_GPIO_PWR_SW;
static const gpio_num_t GPIO_PS_ON = (gpio_num_t)CONFIG_ATX_ESP_GPIO_PS_ON;
static const gpio_num_t GPIO_MB_ACPI = (gpio_num_t)CONFIG_ATX_ESP_GPIO_MB_ACPI;

// Status LED pin (optional)
// TODO: Make it configurable from sdkconfig
static const gpio_num_t GPIO_STATUS_LED = GPIO_NUM_8;

// Last state of MB stored in RTC memory
RTC_DATA_ATTR bool mb_last_state = false;

// Time-related macros
#define ACPI_SIGNAL_MS 500
#define MB_RESTART_WAIT_MS 3000
#define PWR_SW_POLL_MS 10
#define PWR_SW_HOLD_MS 4000

// Status LED blinking
#define STATUS_LED_BLINK_MS 200
#define STATUS_LED_INIT_BLINKS 1
#define STATUS_LED_ERROR_BLINKS 3

/**
 * Waits for given period in milliseconds.
 */
#define WAIT_FOR_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))

/**
 * Blinks the status LED a given number of times.
 */
static esp_err_t status_led_blink(uint8_t blinks)
{
    esp_err_t err = gpio_set_direction(GPIO_STATUS_LED, GPIO_MODE_OUTPUT);
    if (err != ESP_OK)
        return err;

    for (uint8_t i = 0; i < blinks; i++)
    {
        err = gpio_set_level(GPIO_STATUS_LED, 0);
        if (err != ESP_OK)
            return err;

        WAIT_FOR_MS(STATUS_LED_BLINK_MS);

        err = gpio_set_level(GPIO_STATUS_LED, 1);
        if (err != ESP_OK)
            return err;

        WAIT_FOR_MS(STATUS_LED_BLINK_MS);
    }

    return gpio_set_level(GPIO_STATUS_LED, 0);
}

/**
 * Handles errors by blinking the status LED and aborting.
 */
static void handle_error(esp_err_t err)
{
    if (err != ESP_OK)
    {
        status_led_blink(STATUS_LED_ERROR_BLINKS);
        abort();
    }
}

/**
 * Turns on or off the PSU by driving the PS_ON pin.
 */
static esp_err_t psu_set_state(bool on)
{
    int8_t level = on ? 0 : 1; // PS_ON is active-low

    esp_err_t err = gpio_hold_dis(GPIO_PS_ON);
    if (err != ESP_OK)
        return err;

    err = gpio_set_level(GPIO_PS_ON, level);
    if (err != ESP_OK)
        return err;

    return gpio_hold_en(GPIO_PS_ON);
}

/**
 * Sends an ACPI power signal to the motherboard by pulsing the MB_ACPI pin.
 */
static esp_err_t mb_send_acpi_signal()
{
    esp_err_t err = gpio_set_level(GPIO_MB_ACPI, 1);
    if (err != ESP_OK)
        return err;

    WAIT_FOR_MS(ACPI_SIGNAL_MS);

    return gpio_set_level(GPIO_MB_ACPI, 0);
}

/**
 * Reads the current state of the PWR_SW pin.
 * Returns `true` if PWR_SW is pressed, `false` otherwise.
 */
static bool pwr_sw_get_state()
{
    return !gpio_get_level(GPIO_PWR_SW); // PWR_SW is active-low
}

/**
 * Reads the current state of the MB_ON pin.
 * Returns `true` if MB_ON is high, `false` otherwise.
 */
static bool mb_get_state()
{
    return gpio_get_level(GPIO_MB_ON);
}

/**
 * Reads the current state of the PS_ON pin.
 * Returns `true` if PS_ON is high, `false` otherwise.
 */
static bool psu_get_state()
{
    return gpio_get_level(GPIO_PS_ON) == 0;
}

/**
 * Configures the GPIO pins for PWR_SW, MB_ON, PS_ON, and MB_ACPI.
 */
static esp_err_t configure_gpio()
{
    // PS_ON
    gpio_config_t ps_on_config = {
        .pin_bit_mask = BIT(GPIO_PS_ON),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    esp_err_t err = gpio_config(&ps_on_config);
    if (err != ESP_OK)
        return err;

    // PWR_SW
    gpio_config_t pwr_sw_config = {
        .pin_bit_mask = BIT(GPIO_PWR_SW),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };

    err = gpio_config(&pwr_sw_config);
    if (err != ESP_OK)
        return err;

    // MB_ON
    gpio_config_t mb_on_config = {
        .pin_bit_mask = BIT(GPIO_MB_ON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };

    err = gpio_config(&mb_on_config);
    if (err != ESP_OK)
        return err;

    // MB_ACPI
    gpio_config_t mb_acpi_config = {
        .pin_bit_mask = BIT(GPIO_MB_ACPI),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };

    err = gpio_config(&mb_acpi_config);
    if (err != ESP_OK)
        return err;

    return err;
}

/**
 * Handles the MB going down event.
 * Waits to see if it's a shutdown or restart and acts accordingly.
 */
static esp_err_t handle_mb_down_event()
{
    // Detect if it's a shutdown or restart
    WAIT_FOR_MS(MB_RESTART_WAIT_MS);

    // Still off, assume shutdown
    if (!mb_get_state())
        return psu_set_state(false);

    return ESP_OK;
}

/**
 * Handles all MB events.
 */
static esp_err_t handle_mb_event()
{
    // MB went OFF
    if (!mb_get_state())
        return handle_mb_down_event();

    return ESP_OK;
}

/**
 * Handles the PWR_SW press event.
 */
static esp_err_t handle_pwr_sw_pressed_event()
{
    // PSU is OFF, turn it ON
    if (!psu_get_state())
        return psu_set_state(true);

    // PSU is ON

    // MB is ON, send ACPI signal to turn it OFF
    if (mb_get_state())
        return mb_send_acpi_signal();

    // MB is OFF, turn PSU OFF
    return psu_set_state(false);
}

/**
 * Handles the PWR_SW hold event.
 * Forces the PSU to turn off and waits until the button is released.
 */
static esp_err_t handle_pwr_sw_held_event()
{
    esp_err_t err = ESP_OK;
    bool psu_state = psu_get_state();

    // If PSU was ON, force it OFF
    if (psu_state)
        err = psu_set_state(false);

    // Wait until PWR_SW is released
    while (pwr_sw_get_state())
    {
        WAIT_FOR_MS(PWR_SW_POLL_MS);
    }

    // If PSU was OFF, turn it ON
    if (!psu_state)
        err = psu_set_state(true);

    return err;
}

/**
 * Handles the PWR_SW press and hold events.
 * Runs the appropriate handlers.
 */
static esp_err_t handle_pwr_sw_event()
{
    // Distinguish between short press and long hold
    int64_t start_time = esp_timer_get_time();
    while (start_time + PWR_SW_HOLD_MS * 1000 > esp_timer_get_time())
    {
        // Button released
        if (!pwr_sw_get_state())
        {
            return handle_pwr_sw_pressed_event();
        }
        WAIT_FOR_MS(PWR_SW_POLL_MS);
    }

    // Button held for PWR_SW_HOLD_MS
    return handle_pwr_sw_held_event();
}

/**
 * Initializes the system on cold boot.
 * Enables deep sleep hold and ensures the PSU is off.
 */
static esp_err_t initialize(void)
{
    gpio_deep_sleep_hold_en();
    esp_err_t err = psu_set_state(false);
    status_led_blink(STATUS_LED_INIT_BLINKS);
    return err;
}

/**
 * Main application entry point.
 */
void app_main(void)
{
    handle_error(configure_gpio());

    // Switch on the wake up cause
    switch (esp_sleep_get_wakeup_cause())
    {

    // Wake up by GPIO
    case ESP_SLEEP_WAKEUP_GPIO:
    {
        // Waken up from MB
        if (mb_last_state != mb_get_state())
            handle_error(handle_mb_event());

        // Waken up from PWR_SW
        else
            handle_error(handle_pwr_sw_event());

        break;
    }

    // Reset
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
    {
        handle_error(initialize());
    }
    }

    // Configure MB_ON wake up
    if (mb_get_state())
    {
        handle_error(esp_deep_sleep_enable_gpio_wakeup(BIT(GPIO_MB_ON), ESP_GPIO_WAKEUP_GPIO_LOW));
    }
    else
    {
        handle_error(esp_deep_sleep_enable_gpio_wakeup(BIT(GPIO_MB_ON), ESP_GPIO_WAKEUP_GPIO_HIGH));
    }

    // Configure PWR_SW wake up
    handle_error(esp_deep_sleep_enable_gpio_wakeup(BIT(GPIO_PWR_SW), ESP_GPIO_WAKEUP_GPIO_LOW));

    // Store the last MB_ON state
    mb_last_state = mb_get_state();

    // Go to sleep
    esp_deep_sleep_start();
}
