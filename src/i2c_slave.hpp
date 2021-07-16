//! Copyright (c) M5Stack. All rights reserved.
//! Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <cstdint>

#if __has_include(<hal/i2c_types.h>)
 #include <hal/i2c_types.h>
#endif
#if __has_include(<hal/gpio_types.h>)
 #include <hal/gpio_types.h>
#endif
#include <driver/i2c.h>

namespace i2c_slave
{
  bool init(int i2c_num, int pin_sda, int pin_scl, std::uint8_t i2c_addr, void* mainHandle, int intr_alloc_flags);
  bool is_busy(void);
  void start_isr(void);
  void stop_isr(void);
  void reset(void);
  void add_txdata(const std::uint8_t* buf, std::size_t len);
  void add_txdata(std::uint8_t buf);
  void clear_txdata(void);
}
