#include <Arduino.h>
#include <M5GFX.h>
#include <M5UnitLCD.h>
#include <esp_spi_flash.h>

#include "firmware.h"

M5GFX display;
M5UnitLCD display2;

std::uint32_t _crc_table[256];

static void initCRCtable(void)
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

static uint32_t crc32(const std::uint8_t *buf, std::size_t len) {
  std::uint32_t c = 0xffffffff;
  for (std::size_t i = 0; i < len; i++)    {
    c = (c << 8) ^ _crc_table[((c >> 24) ^ buf[i]) & 0xff];
  }
  return c;
}

bool update(void)
{
  display.fillScreen(TFT_WHITE);
  display.setCursor(0, 0);
  display.setFont(&fonts::Font4);
  display.setTextColor(TFT_BLACK, TFT_WHITE);
  display.drawString("UnitLCD", 0, 0);
  display.drawString("update", 0, 28);
  display.fillRect(10, 112, display.width() - 20, 17, TFT_BLACK);
  display.fillCircle(                   10, 120, 8, TFT_BLACK);
  display.fillCircle( display.width() - 10, 120, 8, TFT_BLACK);

  auto panel = (lgfx::Panel_M5UnitLCD*)display2.getPanel();
  auto bus = (lgfx::Bus_I2C*) panel->getBus();
  auto cfg = bus->config();

  std::uint8_t readbuf[8] = { 0 };
  std::size_t length = sizeof(firmware);
  std::size_t block = (length + SPI_FLASH_SEC_SIZE - 1) / SPI_FLASH_SEC_SIZE;

  /// ファームウェア更新コマンド列
  std::uint8_t data[16] = { lgfx::Panel_M5UnitLCD::CMD_UPDATE_BEGIN, 0x77, 0x89, lgfx::Panel_M5UnitLCD::CMD_UPDATE_BEGIN };
  data[4] = length >> 24;
  data[5] = length >> 16;
  data[6] = length >>  8;
  data[7] = length >>  0;

  if (lgfx::i2c::beginTransaction(cfg.i2c_port, cfg.i2c_addr, 400000).has_error()
    || lgfx::i2c::writeBytes(cfg.i2c_port, data, 8).has_error()
    || lgfx::i2c::endTransaction(cfg.i2c_port).has_error())
  {
    return false;
  }

  delay(50);
  data[3] = data[0] = lgfx::Panel_M5UnitLCD::CMD_UPDATE_DATA;

  /// セクタブロック単位(4096Byte) でデータ送信を繰り返す
  for (std::size_t b = 0; b < block; ++b)
  {
    display.fillCircle( 10 + (display.width() - 20) * b / block, 120, 4, TFT_GREEN );
    if (!display.displayBusy())
    {
      display.display();
    }

    auto len = std::min<std::size_t>(SPI_FLASH_SEC_SIZE, length);
    auto crc = crc32(&firmware[b * SPI_FLASH_SEC_SIZE], len);
    data[4] = crc >> 24;  /// 送信するデータのCRC32
    data[5] = crc >> 16;
    data[6] = crc >>  8;
    data[7] = crc >>  0;

    Serial.printf("block %d :", b);
    /// ヘッダおよびデータブロック送信
    if (lgfx::i2c::beginTransaction(cfg.i2c_port, cfg.i2c_addr, 400000).has_error()
    || lgfx::i2c::writeBytes(cfg.i2c_port, data, 8).has_error()
    || lgfx::i2c::writeBytes(cfg.i2c_port, &firmware[b * SPI_FLASH_SEC_SIZE], len).has_error()
    || lgfx::i2c::endTransaction(cfg.i2c_port).has_error())
    {
      Serial.println("fail");
      return false;
    }
    Serial.println("ok");
    /// ビジーチェック
    int retry = 100;
    do
    {
      delay(10);
      readbuf[0] = lgfx::Panel_M5UnitLCD::UPDATE_RESULT_BUSY;
      if (!lgfx::i2c::beginTransaction(cfg.i2c_port, cfg.i2c_addr, 400000, true)
       || !lgfx::i2c::readBytes(cfg.i2c_port, readbuf, 1)
       || !lgfx::i2c::endTransaction(cfg.i2c_port))
      {
        break;
      }
    } while (lgfx::Panel_M5UnitLCD::UPDATE_RESULT_OK != readbuf[0] && --retry);
    if (readbuf[0] != lgfx::Panel_M5UnitLCD::UPDATE_RESULT_OK)
    {
      Serial.printf("fail:%02x\r\n", readbuf[0]);
      return false;
    }
    length -= len;
  }

  data[3] = data[0] = lgfx::Panel_M5UnitLCD::CMD_UPDATE_END;

  lgfx::i2c::endTransaction(cfg.i2c_port);

  if (lgfx::i2c::beginTransaction(cfg.i2c_port, cfg.i2c_addr, 400000).has_error()
    || lgfx::i2c::writeBytes(cfg.i2c_port, data, 4).has_error()
    || lgfx::i2c::endTransaction(cfg.i2c_port).has_error())
  {
    return false;
  }

  return true;
}

bool searchUnitLCD(void)
{
  auto board = display.getBoard();
  if (board == m5gfx::board_t::board_M5Stack)
  {
    if (display2.init(21, 22)) return true;
  }
  else
  if (board == m5gfx::board_t::board_M5Paper)
  {
    lgfx::gpio_hi(5);
    lgfx::pinMode(5, lgfx::pin_mode_t::output);
    if (display2.init(25, 32)) return true;
  }
  else
  if (board == m5gfx::board_t::board_M5StickC
  || board == m5gfx::board_t::board_M5StickCPlus
  || board == m5gfx::board_t::board_M5StackCore2
  || board == m5gfx::board_t::board_M5StackCoreInk
    )
  {
    if (board == m5gfx::board_t::board_M5StackCore2)
    {
      m5gfx::i2c::writeRegister8( 1 , 0x34 , 0x12, 0x40, ~0x00); // EXTEN enable
    }
    if (display2.init(32, 33)) return true;
  }
  if (display2.init(26, 32)) return true; // ATOM
  if (display2.init( 4, 13)) return true; // TimerCam

  return false;
}

void setup(void)
{
  Serial.begin(115200);

  display.init();
  display.setEpdMode(lgfx::epd_mode_t::epd_fastest);

  initCRCtable();

  display.println("search UnitLCD.");
  Serial.println("search UnitLCD.");
  while (!searchUnitLCD())
  {
    delay(100);
  }
}

void loop(void)
{
  display.startWrite();
  if (update())
  {
    display.drawString("success", 0, 56);
    Serial.println("success");
  }
  else
  {
    display.drawString("fail", 0, 56);
    Serial.println("fail");
  }
  display.endWrite();
  delay(8192);
}
