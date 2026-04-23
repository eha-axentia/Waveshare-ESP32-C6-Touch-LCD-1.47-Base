#pragma once

// ============================================================
//  Waveshare ESP32-C6-Touch-LCD-1.47 — pin definitions
//  Source: board schematic (ESP32-C6-Touch-LCD-1.47-Schematic.pdf)
//  Reference: https://www.waveshare.com/wiki/ESP32-C6-Touch-LCD-1.47
// ============================================================

// --- LCD (SPI) -----------------------------------------------
#define LCD_SCLK    1   // SPI clock  (net: LCD_SCL / SD_SCLK)
#define LCD_MOSI    2   // SPI MOSI   (net: LCD_SDA / SD_MOSI)
#define LCD_CS      14  // Chip select
#define LCD_DC      15  // Data / command
#define LCD_RST     22  // Hardware reset (active-low)
#define LCD_BL      23  // Backlight enable (via NPN transistor, HIGH = on)

// Display geometry
#define LCD_WIDTH   172
#define LCD_HEIGHT  320
// ST7789 internal frame-buffer is 240 wide; the 172-px panel sits
// centred, so both column offsets are (240-172)/2 = 34.
#define LCD_COL_OFFSET  34
#define LCD_ROW_OFFSET   0

// --- Touch controller (AXS5106L, I2C) -----------------------
// NOTE: Touch and IMU share the same I2C bus (GPIO18/19).
// Initialise Wire once; both devices communicate on the same bus.
#define TOUCH_SDA   18  // Shared with IMU_SDA
#define TOUCH_SCL   19  // Shared with IMU_SCL
#define TOUCH_RST   20
#define TOUCH_INT   21  // Active-low interrupt
#define TOUCH_ADDR  0x63  // 7-bit I2C address

// --- IMU — QMI8658A (I2C, 6-axis accel + gyro) ---------------
// SA0 pin is tied to GND on this board → address 0x6A.
// SDA/SCL shared with the AXS5106L touch controller.
#define IMU_SDA     18  // Shared with TOUCH_SDA
#define IMU_SCL     19  // Shared with TOUCH_SCL
#define IMU_INT1     5  // Data-ready / interrupt 1
#define IMU_INT2     6  // FIFO / interrupt 2
#define IMU_ADDR    0x6A  // 7-bit I2C address (SA0 = GND)

// --- SD card (SPI, shares CLK/MOSI with LCD) ----------------
#define SD_SCLK     1   // Shared with LCD_SCLK
#define SD_MOSI     2   // Shared with LCD_MOSI
#define SD_MISO     3
#define SD_CS       4

// --- UART (via onboard USB-UART bridge) ----------------------
#define UART_TX     16
#define UART_RX     17

// --- Buttons --------------------------------------------------
#define BOOT_BTN    9   // Active-low boot / flash button

// --- Free GPIOs (exposed on the 22-pin header) ---------------
// GPIO 7, 8, 12, 13 have no dedicated board function.
