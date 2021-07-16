
#include "update.hpp"
#include "common.hpp"

//#include <Update.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_log.h>
#include <esp_attr.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace update
{
  static constexpr std::size_t SKIP_SIZE = 16;
  std::uint8_t _header_buffer[SKIP_SIZE];
  std::uint8_t _buffer[SPI_FLASH_SEC_SIZE];
  std::size_t _bufindex = 0;
  std::size_t _totalindex = 0;
  std::size_t _totalsize = 0;
  std::uint32_t _crc32 = 0;

  std::uint32_t _crc_table[256];
  std::uint32_t _calc_crc32;

  void initCRCtable(void)
  {
    std::size_t i = 0;
    do
    {
      std::uint32_t c = i << 24;
      std::size_t j = 0;
      do
      {
          c = ( c << 1) ^ ( ( c & 0x80000000) ? 0x04C11DB7 : 0);
      } while ( ++j < 8 );
      _crc_table[i] = c;
    } while ( ++i < 256 );
  }

  struct write_info_t
  {
    enum status_t
    { none
    , ok
    , error
    };

    std::uint8_t* buffer = 0;
    std::size_t offset = 0;
    std::size_t len = 0;
    bool finish = false;
    volatile status_t status = status_t::none;
  };

  const esp_partition_t* _partition;

  bool IRAM_ATTR begin(std::size_t totalsize)
  {
    _totalsize = totalsize;
    _totalindex = 0;
    _bufindex = 0;

    _partition = esp_ota_get_next_update_partition(nullptr);
    if (_partition == nullptr)
    {
      ESP_EARLY_LOGE(LOGNAME, "OTA Partition not found");
      return false;
    }
    ESP_EARLY_LOGI(LOGNAME, "OTA Partition: %s", _partition->label);
    return true;
  }

  void IRAM_ATTR setBlockCRC32(std::uint32_t crc32)
  {
    _calc_crc32 = 0xffffffff;
    _crc32 = crc32;
    _bufindex = 0;
  }

  bool IRAM_ATTR addData(std::uint8_t data)
  {
    if (_bufindex >= SPI_FLASH_SEC_SIZE)
    {
      return false;
    }
    _calc_crc32 = (_calc_crc32 << 8) ^ _crc_table[((_calc_crc32 >> 24) ^ data) & 0xff];
    _buffer[_bufindex++] = data;
    ++_totalindex;
    return (_bufindex < SPI_FLASH_SEC_SIZE && _totalindex < _totalsize);
  }

  bool IRAM_ATTR checkCRC32(void)
  {
    return _crc32 == _calc_crc32;
  }

  static void writeTask(void* args)
  {
    auto info = (write_info_t*)args;
    if ((!info->finish && (ESP_OK != esp_partition_erase_range(_partition, info->offset, SPI_FLASH_SEC_SIZE)))
     || (ESP_OK != esp_partition_write(_partition, info->offset, info->buffer, info->len))
     || (info->finish && (ESP_OK != esp_ota_set_boot_partition(_partition))))
    {
      info->status = write_info_t::status_t::error;
    }
    else
    {
      info->status = write_info_t::status_t::ok;
    }
    vTaskDelete(nullptr);
  }

  static bool write(std::uint8_t* buf, std::size_t offset, std::size_t len, bool finish)
  {
    write_info_t info;
    info.buffer = buf;
    info.offset = offset;
    info.len = len;
    info.finish = finish;

    /// Core1で書き込みを行うとクラッシュする事があるためCore0で書き込みを行う
    xTaskCreatePinnedToCore(writeTask, "writeTask", 4096, &info, 2, nullptr, 0);
    while (info.status == write_info_t::status_t::none) taskYIELD();
    return info.status == write_info_t::status_t::ok;
  }

  bool writeBuffer(std::size_t offset)
  {
    auto len = _bufindex;
    if (!len) return false;

    if (offset == 0)
    {
    /// パテーション先頭16バイトを退避して0xFF埋めしておく
    /// （不完全な状態でブートしないようにするため）
      memcpy(_header_buffer, _buffer, SKIP_SIZE);
      memset(_buffer, 0xFF, SKIP_SIZE);
    }
    return write(_buffer, offset, len, false);
  }

  bool IRAM_ATTR end(void)
  {
    /// 退避しておいたパテーション先頭16バイト分のデータを書き込む
    return write(_header_buffer, 0, SKIP_SIZE, true);
  }

};