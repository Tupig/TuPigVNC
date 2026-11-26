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

#ifndef __AUDIOOUTPUTWIN32_H__
#define __AUDIOOUTPUTWIN32_H__

#include <windows.h>
// 定义了 WIN32_LEAN_AND_MEAN 时，windows.h 不会自动引入这个头文件
#include <mmsystem.h>

#include "AudioOutput.h"

class AudioOutputWin32 : public AudioOutput
{
public:
  AudioOutputWin32();
  ~AudioOutputWin32() override;

  bool isAvailable() const { return available; }

  void start() override;
  void stop() override;
  void play(const uint8_t* samples, size_t length) override;

private:
  // 一次送进混音器的异步写入，以及写完之后需要知道的附加信息
  struct Buffer {
    WAVEHDR whdr;
    unsigned long long streamId;
    bool starved;
    unsigned long long starvedAt;
    union {
      Buffer* next;
      PVOID volatile volatileNext;
    };
  };

  bool open();
  size_t getSampleSize() const;
  void addSilence(size_t samples);
  void addSamples(const uint8_t* samples, size_t length);
  void submit();

  static unsigned long long getTimestamp();
  static void CALLBACK waveOutCallback(HWAVEOUT hwo, UINT msg,
                                       DWORD_PTR instance,
                                       DWORD_PTR param1, DWORD_PTR param2);

  bool available, opened;
  HWAVEOUT device;

  // 交给本类、但还没写进设备的样本组成的环形缓冲区。它的容量永远
  // 是 2 的幂，回绕取模的运算依赖这一点。
  uint8_t* buffer;
  size_t bufferSize;
  size_t bufferFree;
  size_t pendingSize;
  size_t writtenHead;
  size_t pendingHead;

  unsigned long long streamId;
  unsigned extraDelayMs;

  // 由设备回调写入；该回调运行在系统选定的线程上，且不能再回调进来
  PVOID volatile doneBuffers;
  LONG volatile buffersInFlight;
};

#endif
