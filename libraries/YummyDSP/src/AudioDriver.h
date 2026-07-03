/*
 * AudioDriver.h - ESP32-P4 Compatible
 * Modified for ESP32-P4 (new I2S driver IDF 5.x+)
 * Base: Gary Grutzek / Junon Matta
 * License: GPLv3
 */

#ifndef AUDIODRIVER_H_
#define AUDIODRIVER_H_

#include "Arduino.h"
#include "driver/gpio.h"

#if CONFIG_IDF_TARGET_ESP32P4
// ESP32-P4 new I2S driver
#include "driver/i2s_std.h"
#include "hal/i2s_types.h"
// Legacy types for compatibility
#include <driver/i2s_types_legacy.h>
#else
// Legacy ESP32 I2S driver
#include "driver/i2s.h"
#endif

enum i2s_alignment {
  CODEC_I2S_ALIGN = 0,
  CODEC_LJ_RJ_ALIGN
};

class AudioDriver {

public:
  static const int BufferSize = 64;
  static const float ScaleFloat2Int;
  static const float ScaleInt2Float;

public:
  AudioDriver();
  ~AudioDriver();

  int setPins(int bitClkPin = 5, int lrClkPin = 6, int dataOutPin = 7, int dataInPin = 4, int enablePin = 8);
  void setI2sPort(i2s_port_t i2s_port);

  int setFormat(int fs = 48000, int channelCount = 2,
                i2s_bits_per_sample_t bitsPerSample = I2S_BITS_PER_SAMPLE_32BIT,
                i2s_comm_format_t format = I2S_COMM_FORMAT_I2S,
                int alignment = CODEC_I2S_ALIGN,
                int mclkFactor = 384,
                int mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX);

  bool start();
  int setup(int fs = 48000, int channelCount = 2,
            int bitClkPin = 5, int lrClkPin = 6,
            int dataOutPin = 7, int dataInPin = 4,
            int enablePin = 8, i2s_port_t i2sPort = I2S_NUM_0);

  bool mute(bool powerDown);
  int readBlock();
  int writeBlock();

  inline int32_t float2Int(float sample) {
    sample *= AudioDriver::ScaleFloat2Int;
    int32_t y = (int32_t)(sample >= 0.5f) ? (int32_t)(sample + 1.0f) : (int32_t)sample;
    y = constrain(y, -AudioDriver::ScaleFloat2Int, AudioDriver::ScaleFloat2Int - 1);
    return (y << 8);
  }

  inline float int2Float(int32_t sample) {
    return (float)(sample * AudioDriver::ScaleInt2Float);
  }

  inline float readSample(int n, int channel) {
    if (channel == 0)
      return int2Float(i2sReadBuffer[chCount * n + channel] << lshift);
    else
      return int2Float(i2sReadBuffer[chCount * n + channel]);
  }

  inline void writeSample(float sample, int n, int channel) {
    i2sWriteBuffer[chCount * n + channel] = float2Int(sample);
  }

  inline void writeStereoSample(float sampleR, float sampleL, int n) {
    int32_t R = float2Int(sampleR);
    int32_t L = float2Int(sampleL);
    i2sWriteBuffer[chCount * n + 0] = L >> lshift;
    i2sWriteBuffer[chCount * n + 1] = R;
  }

  // Raw (no scale)
  inline int32_t raw2Int(float sample) {
    int32_t y = (int32_t)sample;
    return (y << 8);
  }

  inline float int2Raw(int32_t sample) {
    return (float)sample;
  }

  inline float readRawSample(int n, int channel) {
    if (channel == 0)
      return int2Raw(i2sReadBuffer[chCount * n + channel] << lshift);
    else
      return int2Raw(i2sReadBuffer[chCount * n + channel]);
  }

  inline void writeRawStereoSample(float sampleR, float sampleL, int n) {
    int32_t R = raw2Int(sampleR);
    int32_t L = raw2Int(sampleL);
    i2sWriteBuffer[chCount * n + 0] = L >> lshift;
    i2sWriteBuffer[chCount * n + 1] = R;
  }

  inline int readIntSample(int n, int channel) {
    return i2sReadBuffer[chCount * n + channel];
  }

  inline void writeIntSample(int sample, int n, int channel) {
    i2sWriteBuffer[chCount * n + channel] = sample;
  }

protected:
  int fs;
  int chCount;
  int enablePin;
  int lshift = 8;
  i2s_port_t i2sPort;
  int i2sBufferSize;
  int32_t* i2sReadBuffer;
  int32_t* i2sWriteBuffer;
  bool modeRX = true;
  bool modeTX = true;

#if CONFIG_IDF_TARGET_ESP32P4
  i2s_chan_handle_t tx_chan = NULL;
  i2s_chan_handle_t rx_chan = NULL;
  i2s_std_config_t std_cfg;
  bool p4_i2s_initialized = false;
#endif
};

#endif /* AUDIODRIVER_H_ */
