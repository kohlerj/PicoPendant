/*******************************************************************************
 * File:        PicoPendant.cpp
 * Author:      Julien Kohler
 * Created:     2025-04-07
 *
 * Description: Based on CNC-Pendant from Duet and Raspberry Pi Pico Encoder
 *              example.
 * 
 *              Simply add M575 P1 S4 B57600 to your config.g file to enable the
 *              Duet3D pendant support.
 *              P is the serial port number
 *              S4 is the mode. This project uses CRC so mode 4 is required.
 *              B57600 is the baudrate that needs to match the value below
 *              
 *              User can select the axis and the multiplier using the buttons.
 *              The encoder is used to move the selected axis.
 *              The selected axis is indicated by the corresponding LED.
 *              The selected multiplier is indicated by the corresponding LED.
 * 
 *              The user can reset the printer by pressing the X and Z buttons for 
 *              2 seconds.
 * 
 *              The user can home the printer by pressing the Y button for 2 seconds.
 *           
 *
 * License:     This file is subject to the terms and conditions outlined in the
 *              LICENSE file located at the root of this project.
 *              (The LICENSE file may specify MIT, GPL, or another license.)
 ******************************************************************************/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"

#include "quadrature_encoder.pio.h"
#include <string>
#include <string_view>
#include <format>

#define UART uart0

/* Configuration */
static constexpr uint8_t kUartTxPin = 0;
static constexpr uint8_t kUartRxPin = 1;
static constexpr uint32_t kUartBaudRate = 57600; // default Duet3D baud rate for pendant

static constexpr uint8_t kPinAb = 10; // needs to be subsequent pins, e.g. 10-11

static constexpr uint8_t kPin1x = 15;
static constexpr uint8_t kPin1xLed = 14;
static constexpr uint8_t kPin10x = 17;
static constexpr uint8_t kPin10xLed = 16;
static constexpr uint8_t kPin100x = 19;
static constexpr uint8_t kPin100xLed = 18;

static constexpr uint8_t kPinXAxis = 5;
static constexpr uint8_t kPinXAxisLed = 4;
static constexpr uint8_t kPinYAxis = 13;
static constexpr uint8_t kPinYAxisLed = 12;
static constexpr uint8_t kPinZAxis = 21;
static constexpr uint8_t kPinZAxisLed = 20;

static constexpr uint16_t kLongPressLoopDurationInUs = 1000;
static constexpr uint16_t kResetLoopsToTimeout = 2000; // 2000 * 1000us = 2s
static constexpr uint16_t kResetLoopsToToggleLeds = 100; // 100 * 1000us = 100ms
static constexpr uint16_t kHomeLoopsToTimeout = 2000; // 2000 * 1000us = 2s
static constexpr uint16_t kDelayAfterLongPressCommandInMs = 500;

static constexpr int64_t kMinimumTimeBtwCommands = 20 * 1000;  // 20ms
static constexpr int64_t kAccumulatedDeltaDieOutTime =
    kMinimumTimeBtwCommands * 10;  // 10 times the minimum time
/* End of configuration */

static constexpr uint8_t kPulsesPerClick = 4;

static constexpr const char* kMoveCommands[] = {
    "G91 G0 F6000 X",  // X axis
    "G91 G0 F6000 Y",  // Y axis
    "G91 G0 F600 Z",   // Z axis
};

static const uint16_t __not_in_flash("crcdata") kCrc16Table[] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108,
    0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210,
    0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339, 0x8318, 0xb37b,
    0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401,
    0x64e6, 0x74c7, 0x44a4, 0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee,
    0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6,
    0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d,
    0xc7bc, 0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5,
    0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc,
    0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a, 0x6ca6, 0x7c87, 0x4ce4,
    0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd,
    0xad2a, 0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13,
    0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a,
    0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e,
    0xe16f, 0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e, 0x02b1,
    0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb,
    0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0,
    0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799, 0x97b8,
    0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657,
    0x7676, 0x4615, 0x5634, 0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9,
    0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882,
    0x28a3, 0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92, 0xfd2e,
    0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07,
    0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d,
    0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17, 0x7e36, 0x4e55, 0x5e74,
    0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

volatile bool update_leds = true;

enum class Axes : uint8_t {
  kX = 0,
  kY = 1,
  kZ = 2,
};

volatile Axes current_axis = Axes::kX;

namespace multiplier {
static constexpr float k1X = 1.0F;
static constexpr float k10X = 0.1F;
static constexpr float k100X = 0.01F;
}  // namespace multiplier

volatile float current_multiplier = multiplier::k1X;

void __isr GpioCallback(uint gpio, uint32_t events) {
  if (gpio == kPin1x) {
    current_multiplier = multiplier::k1X;
  } else if (gpio == kPin10x) {
    current_multiplier = multiplier::k10X;
  } else if (gpio == kPin100x) {
    current_multiplier = multiplier::k100X;
  } else if (gpio == kPinXAxis) {
    current_axis = Axes::kX;
  } else if (gpio == kPinYAxis) {
    current_axis = Axes::kY;
  } else if (gpio == kPinZAxis) {
    current_axis = Axes::kZ;
  }

  update_leds = true;
}

void LedIntro();
void CheckResetRequest();
void CheckHomingRequest();
uint16_t ComputeCrc16(const std::string_view& data);
void SendCommand(const std::string_view& command);
void UpdateLeds();

int main() {
  int32_t new_value = 0;
  int32_t delta = 0;
  int32_t accumulated_delta = 0;
  int32_t line_number = 0;
  uint32_t long_press_counter = 0;

  absolute_time_t now = 0;
  absolute_time_t last_command_time = 0;
  absolute_time_t last_movement_time = 0;

  uart_init(UART, kUartBaudRate);
  gpio_set_function(kUartTxPin, UART_FUNCSEL_NUM(UART, kUartTxPin));
  gpio_set_function(kUartRxPin, UART_FUNCSEL_NUM(UART, kUartRxPin));

  PIO pio;
  uint sm;
  pio_claim_free_sm_and_add_program(&quadrature_encoder_program, &pio, &sm, NULL);
  quadrature_encoder_program_init(pio, sm, kPinAb, 0);

  gpio_init(kPin1x);
  gpio_pull_up(kPin1x);
  gpio_init(kPin1xLed);
  gpio_set_dir(kPin1xLed, GPIO_OUT != 0U);

  gpio_init(kPin10x);
  gpio_pull_up(kPin10x);
  gpio_init(kPin10xLed);
  gpio_set_dir(kPin10xLed, GPIO_OUT != 0U);

  gpio_init(kPin100x);
  gpio_pull_up(kPin100x);
  gpio_init(kPin100xLed);
  gpio_set_dir(kPin100xLed, GPIO_OUT != 0U);

  gpio_init(kPinXAxis);
  gpio_pull_up(kPinXAxis);
  gpio_init(kPinXAxisLed);
  gpio_set_dir(kPinXAxisLed, GPIO_OUT != 0U);

  gpio_init(kPinYAxis);
  gpio_pull_up(kPinYAxis);
  gpio_init(kPinYAxisLed);
  gpio_set_dir(kPinYAxisLed, GPIO_OUT != 0U);

  gpio_init(kPinZAxis);
  gpio_pull_up(kPinZAxis);
  gpio_init(kPinZAxisLed);
  gpio_set_dir(kPinZAxisLed, GPIO_OUT != 0U);

  LedIntro();

  gpio_set_irq_enabled_with_callback(kPin1x, GPIO_IRQ_EDGE_FALL, true,
                                     &GpioCallback);
  gpio_set_irq_enabled_with_callback(kPin10x, GPIO_IRQ_EDGE_FALL, true,
                                     &GpioCallback);
  gpio_set_irq_enabled_with_callback(kPin100x, GPIO_IRQ_EDGE_FALL, true,
                                     &GpioCallback);
  gpio_set_irq_enabled_with_callback(kPinXAxis, GPIO_IRQ_EDGE_FALL, true,
                                     &GpioCallback);
  gpio_set_irq_enabled_with_callback(kPinYAxis, GPIO_IRQ_EDGE_FALL, true,
                                     &GpioCallback);
  gpio_set_irq_enabled_with_callback(kPinZAxis, GPIO_IRQ_EDGE_FALL, true,
                                     &GpioCallback);

  while (true) {
    CheckResetRequest();

    CheckHomingRequest();

    now = get_absolute_time();

    new_value = quadrature_encoder_get_count(pio, sm);
    static int32_t old_value = new_value;

    delta = new_value - old_value;
    if (delta != 0) {
      last_movement_time = now;
    }
    
    accumulated_delta += delta;
    old_value = new_value;

    if (absolute_time_diff_us(last_command_time, now) >
        kMinimumTimeBtwCommands) {
      if (abs(accumulated_delta) >= kPulsesPerClick) {
        int32_t sending_delta = accumulated_delta / kPulsesPerClick;
        accumulated_delta -= kPulsesPerClick * sending_delta;

        std::string command = kMoveCommands[static_cast<uint8_t>(current_axis)];
        command += std::format("{:.2f}", sending_delta * current_multiplier);

        SendCommand(command);

        last_command_time = now;
      }
    }

    if (absolute_time_diff_us(last_movement_time, now) >
        kAccumulatedDeltaDieOutTime) {
      accumulated_delta = 0;
    }

    UpdateLeds();
  }
}

void UpdateLeds() {
  if (update_leds) {
    update_leds = false;

    // Turn on the LED for the current multiplier
    if (current_multiplier == multiplier::k1X) {
      gpio_put(kPin1xLed, true);
      gpio_put(kPin10xLed, false);
      gpio_put(kPin100xLed, false);
    } else if (current_multiplier == multiplier::k10X) {
      gpio_put(kPin1xLed, false);
      gpio_put(kPin10xLed, true);
      gpio_put(kPin100xLed, false);
    } else if (current_multiplier == multiplier::k100X) {
      gpio_put(kPin1xLed, false);
      gpio_put(kPin10xLed, false);
      gpio_put(kPin100xLed, true);
    }
    // Turn on the LED for the current axis
    if (current_axis == Axes::kX) {
      gpio_put(kPinXAxisLed, true);
      gpio_put(kPinYAxisLed, false);
      gpio_put(kPinZAxisLed, false);
    } else if (current_axis == Axes::kY) {
      gpio_put(kPinXAxisLed, false);
      gpio_put(kPinYAxisLed, true);
      gpio_put(kPinZAxisLed, false);
    } else if (current_axis == Axes::kZ) {
      gpio_put(kPinXAxisLed, false);
      gpio_put(kPinYAxisLed, false);
      gpio_put(kPinZAxisLed, true);
    }
  }
}

void CheckResetRequest() {
  uint16_t long_press_counter = 0;
  bool leds_on = true;
  while (!gpio_get(kPinXAxis) && !gpio_get(kPinZAxis) && gpio_get(kPinYAxis)) {
    long_press_counter++;
    leds_on =
        long_press_counter % kResetLoopsToToggleLeds == 0 ? !leds_on : leds_on;
    gpio_put(kPinXAxisLed, leds_on);
    gpio_put(kPinZAxisLed, leds_on);
    gpio_put(kPinYAxisLed, leds_on);
    if (long_press_counter > kResetLoopsToTimeout) {
      std::string command = "M999";
      SendCommand(command);
      current_axis = Axes::kX;
      sleep_ms(kDelayAfterLongPressCommandInMs);
      break;
    }
    sleep_us(kLongPressLoopDurationInUs);
  }
  update_leds = true;
}

void CheckHomingRequest() {
  uint16_t long_press_counter = 0;
  while (!gpio_get(kPinYAxis) && gpio_get(kPinXAxis) && gpio_get(kPinZAxis)) {
    long_press_counter++;
    static constexpr uint16_t kInterval = kHomeLoopsToTimeout / 4;
    if (long_press_counter < kInterval) {
      gpio_put(kPinXAxisLed, false);
      gpio_put(kPinYAxisLed, false);
      gpio_put(kPinZAxisLed, false);
    } else if (long_press_counter < 2 * kInterval) {
      gpio_put(kPinXAxisLed, true);
      gpio_put(kPinYAxisLed, true);
      gpio_put(kPinZAxisLed, true);
    } else if (long_press_counter < 3 * kInterval) {
      gpio_put(kPinXAxisLed, true);
      gpio_put(kPinYAxisLed, true);
      gpio_put(kPinZAxisLed, false);
    } else if (long_press_counter < 4 * kInterval) {
      gpio_put(kPinXAxisLed, true);
      gpio_put(kPinYAxisLed, false);
      gpio_put(kPinZAxisLed, false);
    } else {
      gpio_put(kPinXAxisLed, false);
      gpio_put(kPinYAxisLed, false);
      gpio_put(kPinZAxisLed, false);
    }

    if (long_press_counter > kHomeLoopsToTimeout) {
      std::string command = "G28";
      SendCommand(command);
      current_axis = Axes::kX;
      sleep_ms(kDelayAfterLongPressCommandInMs);
      break;
    }
    sleep_us(kLongPressLoopDurationInUs);
  }
  update_leds = true;
}

void SendCommand(const std::string_view& command) {
  static uint32_t line_number = 0;
  static uint16_t crc = 0;

  std::string command_buffer;
  command_buffer.reserve(50);

  command_buffer = "N";
  command_buffer += std::to_string(line_number++);
  command_buffer += " ";
  command_buffer += command;
  crc = ComputeCrc16(command_buffer);
  command_buffer += "*";
  command_buffer += std::format("{:05}", crc);
  command_buffer += "\n";

  uart_default_tx_wait_blocking();
  uart_puts(UART, command_buffer.c_str());
}

void LedIntro() {
  static constexpr uint32_t kOnDurationUs = 50 * 1000;
  static constexpr uint32_t kOffDurationUs = 100 * 1000;

  for (int i = 0; i < 3; ++i) {
    gpio_put(kPin1xLed, true);
    sleep_us(kOnDurationUs);
    gpio_put(kPin1xLed, false);
    sleep_us(kOffDurationUs);

    gpio_put(kPin10xLed, true);
    sleep_us(kOnDurationUs);
    gpio_put(kPin10xLed, false);
    sleep_us(kOffDurationUs);

    gpio_put(kPin100xLed, true);
    sleep_us(kOnDurationUs);
    gpio_put(kPin100xLed, false);
    sleep_us(kOffDurationUs);

    gpio_put(kPinXAxisLed, true);
    sleep_us(kOnDurationUs);
    gpio_put(kPinXAxisLed, false);
    sleep_us(kOffDurationUs);

    gpio_put(kPinYAxisLed, true);
    sleep_us(kOnDurationUs);
    gpio_put(kPinYAxisLed, false);
    sleep_us(kOffDurationUs);

    gpio_put(kPinZAxisLed, true);
    sleep_us(kOnDurationUs);
    gpio_put(kPinZAxisLed, false);
    sleep_us(kOffDurationUs);
  }

  sleep_ms(200);

  gpio_put(kPin1xLed, true);
  gpio_put(kPin10xLed, true);
  gpio_put(kPin100xLed, true);
  gpio_put(kPinXAxisLed, true);
  gpio_put(kPinYAxisLed, true);
  gpio_put(kPinZAxisLed, true);

  sleep_ms(1000);

  gpio_put(kPin1xLed, false);
  gpio_put(kPin10xLed, false);
  gpio_put(kPin100xLed, false);
  gpio_put(kPinXAxisLed, false);
  gpio_put(kPinYAxisLed, false);
  gpio_put(kPinZAxisLed, false);
}

uint16_t ComputeCrc16(const std::string_view& data) {
  uint16_t crc = 0x0;  // Initial value
  for (char ch : data) {
    uint8_t byte = static_cast<uint8_t>(ch);
    crc = (crc << 8) ^ kCrc16Table[((crc >> 8) ^ byte) & 0x00ff];
  }
  return crc;
}
