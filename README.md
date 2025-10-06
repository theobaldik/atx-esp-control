# ATX-ESP Control

This ESP32-based project enables turning on and off any embedded system powered from an ATX PSU using a power button. If ACPI input pin is present on the board, the ESP can also perform ACPI reset.

The motivation for creating this project was to control power state of RK3588 NAS Kit connected to ATX power supply using the standard PC case PWR_SW button.

Preffered ESP32 variant is ESP32-S3, as it is very cheap and power efficient, but any ESP32 can be used.
