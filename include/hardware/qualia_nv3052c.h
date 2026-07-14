#pragma once

#if defined(TARGET_QUALIA_S3)

/**
 * Reset and register-initialize the NV3052C 4" round 720x720 panel on the
 * Adafruit Qualia ESP32-S3.
 *
 * The panel's config lines (CS/CLK/MOSI/RESET) hang off the on-board TCA9554
 * I2C expander (addr 0x3F), not GPIOs, so the init is a bit-banged 9-bit SPI
 * transaction clocked through the expander. This must be called once, before
 * LovyanGFX starts streaming pixels via Bus_RGB (i.e. before tft.init()).
 *
 * Returns true if the expander acknowledged on I2C.
 */
bool qualiaPanelInit();

#endif  // TARGET_QUALIA_S3
