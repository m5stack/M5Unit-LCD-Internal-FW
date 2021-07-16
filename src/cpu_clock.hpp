//! Copyright (c) M5Stack. All rights reserved.
//! Licensed under the MIT license. See LICENSE file in the project root for full license information.

namespace cpu_clock
{
  enum cpu_clock_t
  { clock_8MHz
  , clock_10MHz
  , clock_20MHz
  , clock_40MHz
  , clock_80MHz
  , clock_160MHz
  , clock_240MHz
  , clock_MAX
  };

  void init(void);
  void set_cpu_clock(cpu_clock_t clock);
  void set_clock_limit(cpu_clock_t clock_min, cpu_clock_t clock_max);
  void request_clock_up(cpu_clock_t clock);
  void request_clock_down(cpu_clock_t clock);
}
