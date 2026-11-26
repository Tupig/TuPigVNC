/* Copyright 2022 Mikhail Kupchik
 * Copyright 2026 jose-pr
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifndef __AUDIOOUTPUT_H__
#define __AUDIOOUTPUT_H__

#include <stddef.h>
#include <stdint.h>

#include <rfb/qemuTypes.h>

// 在 CConnection 里向服务端请求的音频格式，所有后端都必须按这个格式播放
const uint8_t audioSampleFormat = rfb::qemuAudioFormatS16;
const uint8_t audioChannels = 2;
const uint32_t audioFrequency = 48000;

// 预缓冲多少音频，以及新流开始前先播多少静音，用来掩盖样本到达
// 网络时间的抖动。这两个值都不取决于具体用哪套声音系统播放。
const unsigned audioMaxJitterMs = 1000;
const unsigned audioMinStreamDelayMs = 20;

class AudioOutput
{
public:
  virtual ~AudioOutput() {}

  static AudioOutput* create();

  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void play(const uint8_t* samples, size_t length) = 0;
};

#endif
