# ATX-ESP Control

This ESP-IDF based project enables turning on and off any embedded system powered from an ATX PSU using a power button. If ACPI input pin is present on the board, the ESP can also perform ACPI shutdown.

The motivation for creating this project was to control power state of [RK3588 NAS Kit](https://wiki.friendlyelec.com/wiki/index.php/CM3588) connected to ATX power supply using the standard PC case's PWR_SW button.

The current version is written for ESP32-C3 target and was only tested on the [Super Mini devkit](https://www.espboards.dev/esp32/esp32-c3-super-mini/). It might be possible to support other targets with some code modifications. E.g. EPS32 uses [external wakeups](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/sleep_modes.html#external-wakeup-ext0) instead of ESP32-C3's [GPIO wakeup](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32c3/api-reference/system/sleep_modes.html#gpio-wakeup).

> [!NOTE]
> I cannot get working UART logging along with the deep sleep wake ups. That is why the logging and UART communication is disabled by default.

## Wiring

The wiring is showed in the [docs/schematic.pdf](docs/schematic.pdf). The document shows wiring for ESP32-C3 Super Mini devkit, standard ATX 24-pin connector of the PSU, RK3588 NAS Kit and standard PC case's PWR_SW + PWR_LED.

It shall be relatively easy to adopt the wiring to another motherboard (like Raspberry Pi) as the only requirement is the 3.3V output signal, which shall match the ON/OFF status of the motherboard.

## Installation

To flash the image, use either ESP-IDF or esptool.

1. Download the latest binary image from the release section
2. Connect the ESP32-S3 board to the PC
3. Flash the image using `idf.py flash` (or `esptool write_flash`)

> [!NOTE]
> For more information about flashing refer to [idf.py docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-py.html#flash-the-project-flash) or [esptool docs](https://docs.espressif.com/projects/esptool/en/latest/esp32/).

## Building from source

The recommended way of building the project from source is to use [ESP-IDF (v5.1)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html):

1. Generate `sdkconfig` using `idf.py menuconfig`
2. Build the project using `idf.py build`
3. Flash it to the ESP32-C3 chip using `idf.py flash`
