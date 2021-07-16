//! Copyright (c) M5Stack. All rights reserved.
//! Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <driver/rtc_io.h>
#include <soc/rtc.h>
#include <algorithm>

#include "cpu_clock.hpp"

#include <M5GFX.h>

namespace cpu_clock
{
  rtc_cpu_freq_config_t _cpu_freq_conf[clock_MAX];
  cpu_clock_t _clock_min    = clock_80MHz;
  cpu_clock_t _clock_max    = clock_160MHz;
  cpu_clock_t _now_clock     = clock_MAX;  
  cpu_clock_t _request_clock = clock_240MHz;

  void IRAM_ATTR set_cpu_clock(cpu_clock_t clock)
  {
    if (_now_clock != clock)
    {
   if (_now_clock < clock) lgfx::gpio_hi(0);
   else lgfx::gpio_lo(0);
  //ESP_LOGI("UnitLCD","set_clock:%d", clock);
      _now_clock = clock;
      rtc_clk_cpu_freq_set_config_fast(&_cpu_freq_conf[clock]);
    }
  }

  void init(void)
  {
    rtc_clk_cpu_freq_mhz_to_config(240, &_cpu_freq_conf[cpu_clock_t::clock_240MHz]);
    rtc_clk_cpu_freq_mhz_to_config(160, &_cpu_freq_conf[cpu_clock_t::clock_160MHz]);
    rtc_clk_cpu_freq_mhz_to_config( 80, &_cpu_freq_conf[cpu_clock_t::clock_80MHz]);
    rtc_clk_cpu_freq_mhz_to_config( 40, &_cpu_freq_conf[cpu_clock_t::clock_40MHz]);
    rtc_clk_cpu_freq_mhz_to_config( 20, &_cpu_freq_conf[cpu_clock_t::clock_20MHz]);
    rtc_clk_cpu_freq_mhz_to_config( 10, &_cpu_freq_conf[cpu_clock_t::clock_10MHz]);
    rtc_clk_cpu_freq_mhz_to_config(  8, &_cpu_freq_conf[cpu_clock_t::clock_8MHz]);
  }

  void IRAM_ATTR request_clock_up(cpu_clock_t clock)
  {
    clock = std::min(clock, _clock_max);
    if (_request_clock < clock)
    {
      _request_clock = clock;
      set_cpu_clock(clock);
    }
  }

  void IRAM_ATTR request_clock_down(cpu_clock_t clock)
  {
    clock = std::max(clock, _clock_min);
    if (_request_clock > clock)
    {
      _request_clock = clock;
      set_cpu_clock(clock);
    }
  }

  void IRAM_ATTR set_clock_limit(cpu_clock_t clock_min, cpu_clock_t clock_max)
  {
    _clock_min = clock_min;
    _clock_max = clock_max;
    set_cpu_clock(std::min(clock_max, std::max(clock_min, _request_clock)));
  }
}
