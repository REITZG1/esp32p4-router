/*
 * AudioDriver.cpp - ESP32-P4 Compatible
 * Modified for ESP32-P4 (new I2S driver IDF 5.x+)
 * Base: Gary Grutzek / Junon Matta
 * License: GPLv3
 */

#include "AudioDriver.h"

#if CONFIG_IDF_TARGET_ESP32P4
#include <driver/i2s_common.h>
#endif

const float AudioDriver::ScaleFloat2Int = 2147483647.0f;
const float AudioDriver::ScaleInt2Float = 4.6566129e-10f; // 1.0f / 2147483647.0f

AudioDriver::AudioDriver() {
  i2sPort = I2S_NUM_0;
  fs = 48000;
  chCount = 2;
  enablePin = -1;
  i2sReadBuffer = NULL;
  i2sWriteBuffer = NULL;
}

AudioDriver::~AudioDriver() {
  if (i2sReadBuffer) free(i2sReadBuffer);
  if (i2sWriteBuffer) free(i2sWriteBuffer);
#if CONFIG_IDF_TARGET_ESP32P4
  if (tx_chan) i2s_del_channel(tx_chan);
  if (rx_chan) i2s_del_channel(rx_chan);
#endif
}

void AudioDriver::setI2sPort(i2s_port_t port) {
  i2sPort = port;
}

int AudioDriver::setPins(int bitClkPin, int lrClkPin, int dataOutPin, int dataInPin, int enablePin) {
#if CONFIG_IDF_TARGET_ESP32P4
  // P4 uses i2s_std_config for pin mapping
  std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(fs);
  std_cfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
  std_cfg.gpio_cfg = {
    .mclk = I2S_GPIO_UNUSED,
    .bclk = (gpio_num_t)bitClkPin,
    .ws = (gpio_num_t)lrClkPin,
    .dout = (gpio_num_t)dataOutPin,
    .din = (gpio_num_t)dataInPin,
    .invert_flags = {
      .mclk_inv = false,
      .bclk_inv = false,
      .ws_inv = false
    }
  };
#else
  i2s_pin_config_t pin_config = {
    .bck_io_num = bitClkPin,
    .ws_io_num = lrClkPin,
    .data_out_num = dataOutPin,
    .data_in_num = dataInPin
  };
  i2s_set_pin(i2sPort, &pin_config);
#endif
  this->enablePin = enablePin;
  if (enablePin >= 0) {
    pinMode(enablePin, OUTPUT);
    digitalWrite(enablePin, HIGH);
  }
  return 0;
}

int AudioDriver::setFormat(int fs, int channelCount, i2s_bits_per_sample_t bitsPerSample,
                           i2s_comm_format_t format, int alignment,
                           int mclkFactor, int mode) {
  this->fs = fs;
  this->chCount = channelCount;

#if CONFIG_IDF_TARGET_ESP32P4
  modeRX = mode & I2S_MODE_RX;
  modeTX = mode & I2S_MODE_TX;

  std_cfg.clk_cfg.sample_rate_hz = fs;
  std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

  i2s_slot_mode_t slot_mode = (channelCount > 1) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO;
  std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG((i2s_data_bit_width_t)bitsPerSample, slot_mode);

  I2S_COMM_FORMAT_I2S; // Philips format is default for STD mode
#else
  i2s_config = {
    .mode = (i2s_mode_t)mode,
    .sample_rate = fs,
    .bits_per_sample = bitsPerSample,
    .channel_format = (channelCount > 1) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = format,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BufferSize,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = mclkFactor * fs
  };

  if (alignment == CODEC_LJ_RJ_ALIGN) {
    i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB);
  }
#endif
  return 0;
}

bool AudioDriver::start() {
  int err = 0;

#if CONFIG_IDF_TARGET_ESP32P4
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2sPort, I2S_ROLE_MASTER);

  if (modeTX) {
    err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (err != ESP_OK) return false;
    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (err != ESP_OK) return false;
    err = i2s_channel_enable(tx_chan);
    if (err != ESP_OK) return false;
  }

  if (modeRX) {
    err = i2s_new_channel(&chan_cfg, NULL, &rx_chan);
    if (err != ESP_OK) return false;
    err = i2s_channel_init_std_mode(rx_chan, &std_cfg);
    if (err != ESP_OK) return false;
    err = i2s_channel_enable(rx_chan);
    if (err != ESP_OK) return false;
  }

  p4_i2s_initialized = true;
#else
  err = i2s_driver_install(i2sPort, &i2s_config, 0, NULL);
  if (err != ESP_OK) return false;
#endif

  i2sBufferSize = BufferSize * chCount;
  i2sReadBuffer = (int32_t*)malloc(i2sBufferSize * sizeof(int32_t));
  i2sWriteBuffer = (int32_t*)malloc(i2sBufferSize * sizeof(int32_t));

  if (!i2sReadBuffer || !i2sWriteBuffer) return false;
  memset(i2sWriteBuffer, 0, i2sBufferSize * sizeof(int32_t));

  return true;
}

int AudioDriver::setup(int fs, int channelCount, int bitClkPin, int lrClkPin,
                       int dataOutPin, int dataInPin, int enablePin, i2s_port_t i2sPort) {
  setI2sPort(i2sPort);
  setFormat(fs, channelCount);
  setPins(bitClkPin, lrClkPin, dataOutPin, dataInPin, enablePin);
  if (!start()) return -1;
  return 0;
}

bool AudioDriver::mute(bool powerDown) {
  if (enablePin >= 0) {
    digitalWrite(enablePin, powerDown ? LOW : HIGH);
  }
  return true;
}

int AudioDriver::readBlock() {
  size_t bytesRead = 0;
  esp_err_t err;

#if CONFIG_IDF_TARGET_ESP32P4
  if (rx_chan) {
    err = i2s_channel_read(rx_chan, i2sReadBuffer, i2sBufferSize * sizeof(int32_t), &bytesRead, portMAX_DELAY);
  } else {
    memset(i2sReadBuffer, 0, i2sBufferSize * sizeof(int32_t));
    bytesRead = i2sBufferSize * sizeof(int32_t);
    return 0;
  }
#else
  err = i2s_read(i2sPort, i2sReadBuffer, i2sBufferSize * sizeof(int32_t), &bytesRead, portMAX_DELAY);
#endif

  if (err != ESP_OK) return -1;
  if (bytesRead < i2sBufferSize * sizeof(int32_t)) return -1;
  return 0;
}

int AudioDriver::writeBlock() {
  size_t bytesWritten = 0;
  esp_err_t err;

#if CONFIG_IDF_TARGET_ESP32P4
  if (tx_chan) {
    err = i2s_channel_write(tx_chan, i2sWriteBuffer, i2sBufferSize * sizeof(int32_t), &bytesWritten, portMAX_DELAY);
  } else {
    return -1;
  }
#else
  err = i2s_write(i2sPort, i2sWriteBuffer, i2sBufferSize * sizeof(int32_t), &bytesWritten, portMAX_DELAY);
#endif

  if (err != ESP_OK) return -1;
  if (bytesWritten < i2sBufferSize * sizeof(int32_t)) return -1;
  return 0;
}
