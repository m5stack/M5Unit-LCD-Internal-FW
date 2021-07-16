//! Copyright (c) M5Stack. All rights reserved.
//! Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <driver/i2c.h>
#include <driver/rtc_io.h>
//#include <driver/timer.h>
#include <driver/periph_ctrl.h>
#include <soc/i2c_reg.h>
#include <soc/i2c_struct.h>
#include <soc/rtc.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "command_processor.hpp"
#include "i2c_slave.hpp"

namespace i2c_slave
{
  static constexpr std::size_t soc_i2c_fifo_len = 32;
  static constexpr std::uint32_t i2c_intr_mask = 0x3fff;  /*!< I2C all interrupt bitmap */
  static constexpr std::uint32_t I2C_FIFO_FULL_THRESH_VAL      = 1;
  static constexpr std::uint32_t I2C_FIFO_EMPTY_THRESH_VAL     = 2;
  static constexpr std::uint32_t I2C_SLAVE_TIMEOUT_DEFAULT     = 0xFFFFF; /* I2C slave timeout value, APB clock cycle number */
  static constexpr std::uint32_t I2C_SLAVE_SDA_SAMPLE_DEFAULT  = 4;       /* I2C slave sample time after scl positive edge default value */
  static constexpr std::uint32_t I2C_SLAVE_SDA_HOLD_DEFAULT    = 4;       /* I2C slave hold time after scl negative edge default value */

  struct i2c_obj_t
  {
    intr_handle_t intr_handle;  // I2C interrupt handle
    i2c_port_t i2c_num;   // I2C port number
    xTaskHandle main_handle = nullptr;
    std::uint32_t addr;   // I2C slave addr
  };

  i2c_obj_t i2c_obj;

  bool IRAM_ATTR is_busy(void)
  {
    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;
    return dev->status_reg.bus_busy;
  }

  static void IRAM_ATTR i2c_isr_handler(void *arg)
  {
    auto p_i2c = (i2c_obj_t*)arg;
    auto dev = p_i2c->i2c_num == 0 ? &I2C0 : &I2C1;

    std::uint32_t rx_fifo_cnt = dev->status_reg.rx_fifo_cnt;
    typeof(dev->int_status) int_sts;
    int_sts.val = dev->int_status.val;
    if (rx_fifo_cnt)
    {
      bool notify = false;
      do
      {
        if (command_processor::addData(dev->fifo_data.data))
        {
          notify = true;
        }  
      } while (--rx_fifo_cnt);
      if (notify)
      {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(p_i2c->main_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR();
      }
    }
    if (int_sts.tx_fifo_empty)
    {
      command_processor::prepareTxData();
    }
    if (int_sts.trans_complete || int_sts.trans_start || int_sts.arbitration_lost)
    {
      command_processor::closeData();
    }
    dev->int_clr.val = int_sts.val;
  }

  void IRAM_ATTR clear_txdata(void)
  {
    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;
    dev->int_ena.tx_fifo_empty = false;
    dev->fifo_conf.tx_fifo_rst = 1;
    dev->fifo_conf.tx_fifo_rst = 0;
  }

  void IRAM_ATTR add_txdata(const std::uint8_t* buf, std::size_t len)
  {
    uint32_t fifo_addr = (i2c_obj.i2c_num == 0) ? 0x6001301c : 0x6002701c;
    for (std::size_t i = 0; i < len; ++i)
    {
      WRITE_PERI_REG(fifo_addr, buf[i]);
    }
    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;
    dev->int_clr.tx_fifo_empty = true;
    dev->int_ena.tx_fifo_empty = true;
  }

  void IRAM_ATTR add_txdata(std::uint8_t buf)
  {
    uint32_t fifo_addr = (i2c_obj.i2c_num == 0) ? 0x6001301c : 0x6002701c;
    WRITE_PERI_REG(fifo_addr, buf);
    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;
    dev->int_clr.tx_fifo_empty = true;
    dev->int_ena.tx_fifo_empty = true;
  }

  void IRAM_ATTR start_isr(void)
  {
    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;
    dev->int_ena.val = I2C_RXFIFO_FULL_INT_ENA
                     | I2C_TRANS_COMPLETE_INT_ENA
                     | I2C_ARBITRATION_LOST_INT_ENA
                     | I2C_TXFIFO_EMPTY_INT_ENA
                     | I2C_TRANS_START_INT_ENA
                     ;
  /*
  | I2C_ACK_ERR_INT_ENA_M
  | I2C_END_DETECT_INT_ENA_M
  | I2C_MASTER_TRAN_COMP_INT_ENA_M
  | I2C_SLAVE_TRAN_COMP_INT_ENA_M
  | I2C_TIME_OUT_INT_ENA_M
  | I2C_TX_SEND_EMPTY_INT_ENA_M
  | 0x3FFF
  //*/
  }

  void IRAM_ATTR stop_isr(void)
  {
    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;
    dev->int_ena.val = 0;
    dev->int_clr.val = i2c_intr_mask;
  }

  void IRAM_ATTR i2c_periph_start(void)
  {
    stop_isr();
    periph_module_enable((i2c_obj.i2c_num == 0) ? PERIPH_I2C0_MODULE : PERIPH_I2C1_MODULE);

    auto dev = i2c_obj.i2c_num == 0 ? &I2C0 : &I2C1;

    typeof(dev->ctr) ctrl_reg;
    ctrl_reg.val = 0;
    ctrl_reg.sda_force_out = 1;
    ctrl_reg.scl_force_out = 1;
    dev->ctr.val = ctrl_reg.val;

    typeof(dev->fifo_conf) fifo_conf;
    fifo_conf.val = 0;
    fifo_conf.rx_fifo_full_thrhd = I2C_FIFO_FULL_THRESH_VAL;
    fifo_conf.tx_fifo_empty_thrhd = I2C_FIFO_EMPTY_THRESH_VAL;
    dev->fifo_conf.val = fifo_conf.val;

    dev->slave_addr.addr = i2c_obj.addr;
    dev->slave_addr.en_10bit = 0;

    dev->sda_hold.time = I2C_SLAVE_SDA_HOLD_DEFAULT;
    dev->sda_sample.time = I2C_SLAVE_SDA_SAMPLE_DEFAULT;
    dev->timeout.tout = I2C_SLAVE_TIMEOUT_DEFAULT;

    dev->scl_filter_cfg.en = 1;
    dev->scl_filter_cfg.thres = 0;
    dev->sda_filter_cfg.en = 1;
    dev->sda_filter_cfg.thres = 0;
    start_isr();
  }

  bool IRAM_ATTR init(int i2c_num, int pin_sda, int pin_scl, std::uint8_t i2c_addr, void* mainHandle, int intr_alloc_flags)
  {
    i2c_obj.i2c_num = (i2c_port_t)i2c_num;
    i2c_obj.addr = i2c_addr;
    i2c_obj.main_handle = mainHandle;

    if ((ESP_OK == i2c_set_pin(i2c_obj.i2c_num, pin_sda, pin_scl, GPIO_PULLUP_ENABLE, GPIO_PULLUP_ENABLE, I2C_MODE_SLAVE))
    && (ESP_OK == esp_intr_alloc( (i2c_num == 0)
                          ? ETS_I2C_EXT0_INTR_SOURCE 
                          : ETS_I2C_EXT1_INTR_SOURCE
                        , intr_alloc_flags
                        , i2c_isr_handler
                        , &i2c_obj
                        , &(i2c_obj.intr_handle)
                        )))
    {
      i2c_periph_start();
      return true;
    }
    return false;
  }

  void IRAM_ATTR reset(void)
  {
    auto mod = i2c_obj.i2c_num == 0 ? PERIPH_I2C0_MODULE : PERIPH_I2C1_MODULE;
    periph_module_disable(mod);
    i2c_periph_start();
  }

}
