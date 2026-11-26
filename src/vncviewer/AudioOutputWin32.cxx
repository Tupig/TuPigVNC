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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>

#include <core/LogWriter.h>
#include <core/i18n.h>

#include <rfb/qemuTypes.h>

#include "AudioOutputWin32.h"

static core::LogWriter vlog("AudioOutputWin32");

static void fillWaveFormat(WAVEFORMATEX* wfx)
{
  memset(wfx, 0, sizeof(*wfx));
  wfx->wFormatTag = WAVE_FORMAT_PCM;
  wfx->nChannels = audioChannels;
  wfx->nSamplesPerSec = audioFrequency;
  // 样本格式按位宽两两成对排列，先无符号再有符号
  wfx->wBitsPerSample = 8 << (audioSampleFormat >> 1);
  wfx->nBlockAlign = audioChannels * (wfx->wBitsPerSample / 8);
  wfx->nAvgBytesPerSec = wfx->nSamplesPerSec * wfx->nBlockAlign;
  wfx->cbSize = 0;
}

AudioOutputWin32::AudioOutputWin32()
  : available(false), opened(false), device(nullptr),
    buffer(nullptr), bufferSize(0), bufferFree(0), pendingSize(0),
    writtenHead(0), pendingHead(0),
    streamId(0), extraDelayMs(0),
    doneBuffers(nullptr), buffersInFlight(0)
{
  WAVEFORMATEX wfx;

  fillWaveFormat(&wfx);

  if (waveOutOpen(nullptr, WAVE_MAPPER, &wfx, 0, 0,
                  CALLBACK_NULL | WAVE_FORMAT_QUERY) != MMSYSERR_NOERROR) {
    vlog.debug("No audio playback device supports the format we need");
    return;
  }

  available = true;
}

AudioOutputWin32::~AudioOutputWin32()
{
  Buffer* done;

  if (!opened)
    return;

  // 取消仍在播放的内容，这样我们交出去的每个缓冲区都会先还回来，
  // 之后才能释放
  waveOutReset(device);

  done = (Buffer*)InterlockedExchangePointer(&doneBuffers, nullptr);
  while (done != nullptr) {
    Buffer* next = done->next;
    waveOutUnprepareHeader(device, &done->whdr, sizeof(WAVEHDR));
    free(done);
    done = next;
  }

  waveOutClose(device);

  free(buffer);
}

size_t AudioOutputWin32::getSampleSize() const
{
  return audioChannels << (audioSampleFormat >> 1);
}

bool AudioOutputWin32::open()
{
  WAVEFORMATEX wfx;
  size_t samples, sampleSize;

  if (opened)
    return true;
  if (!available)
    return false;

  fillWaveFormat(&wfx);

  if (waveOutOpen(&device, WAVE_MAPPER, &wfx,
                  (DWORD_PTR)&AudioOutputWin32::waveOutCallback,
                  (DWORD_PTR)this, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
    vlog.error(_("Could not open audio playback device"));
    available = false;
    return false;
  }

  // 把样本数向上取整到 2 的幂，这样下面的回绕运算就能用掩码实现。
  // 样本大小同样是 2 的幂，所以字节数最终也是 2 的幂。
  samples = 1;
  while (samples < (4 * audioMaxJitterMs * audioFrequency) / 1000)
    samples <<= 1;

  sampleSize = getSampleSize();

  buffer = (uint8_t*)calloc(samples, sampleSize);
  if (buffer == nullptr) {
    waveOutClose(device);
    device = nullptr;
    available = false;
    return false;
  }

  bufferSize = bufferFree = samples * sampleSize;
  pendingSize = writtenHead = pendingHead = 0;

  opened = true;

  return true;
}

void AudioOutputWin32::addSilence(size_t samples)
{
  size_t left;

  left = samples * getSampleSize();

  while (left > 0) {
    size_t chunk = left;

    if (chunk > bufferFree)
      chunk = bufferFree;
    if (chunk > bufferSize - pendingHead)
      chunk = bufferSize - pendingHead;
    if (chunk == 0)
      break;

    memset(buffer + pendingHead,
           audioSampleFormat == rfb::qemuAudioFormatU8 ? 0x80 : 0x00,
           chunk);

    pendingHead = (pendingHead + chunk) & (bufferSize - 1);
    bufferFree -= chunk;
    pendingSize += chunk;
    left -= chunk;
  }
}

void AudioOutputWin32::addSamples(const uint8_t* samples, size_t length)
{
  // 不完整的样本对谁都没用，还会让它后面每个声道都错位
  length -= length % getSampleSize();

  while (length > 0) {
    size_t chunk = length;

    if (chunk > bufferFree)
      chunk = bufferFree;
    if (chunk > bufferSize - pendingHead)
      chunk = bufferSize - pendingHead;
    if (chunk == 0) {
      // 我们落后的量已经超过整个缓冲区的长度，所以此刻丢弃的样本
      // 本来就永远来不及播放了
      vlog.debug("Audio buffer full, discarding %d bytes", (int)length);
      break;
    }

    memcpy(buffer + pendingHead, samples, chunk);

    pendingHead = (pendingHead + chunk) & (bufferSize - 1);
    bufferFree -= chunk;
    pendingSize += chunk;
    samples += chunk;
    length -= chunk;
  }
}

unsigned long long AudioOutputWin32::getTimestamp()
{
  FILETIME now;
  ULARGE_INTEGER value;

  GetSystemTimeAsFileTime(&now);

  value.LowPart = now.dwLowDateTime;
  value.HighPart = now.dwHighDateTime;

  return value.QuadPart;
}

// 系统用完一个缓冲区后，会在它自己的线程上回调这里。这里不能调用
// 任何 waveOut 函数，所以只把该缓冲区挂到一个链表上，交给 submit()
// 处理。
void CALLBACK AudioOutputWin32::waveOutCallback(HWAVEOUT hwo, UINT msg,
                                                DWORD_PTR instance,
                                                DWORD_PTR param1,
                                                DWORD_PTR /*param2*/)
{
  AudioOutputWin32* self = (AudioOutputWin32*)instance;
  Buffer* buf = (Buffer*)param1;
  PVOID head;

  if (msg != WOM_DONE)
    return;
  if (!self->opened || (self->device != hwo))
    return;
  if (!(buf->whdr.dwFlags & WHDR_DONE))
    return;

  // 没有东西可播，说明设备在等我们时断流了；它断流多久，就是
  // 我们还需要多提前缓冲多少
  if (InterlockedDecrement(&self->buffersInFlight) == 0) {
    buf->starved = true;
    buf->starvedAt = getTimestamp();
  }

  head = self->doneBuffers;
  while (true) {
    PVOID previous;

    InterlockedExchangePointer(&buf->volatileNext, head);
    previous = InterlockedCompareExchangePointer(&self->doneBuffers,
                                                 buf, head);
    if (previous == head)
      break;
    head = previous;
  }
}

void AudioOutputWin32::submit()
{
  Buffer* spare;

  if (!opened)
    return;

  spare = (Buffer*)InterlockedExchangePointer(&doneBuffers, nullptr);

  for (Buffer* buf = spare; buf != nullptr; buf = buf->next) {
    bufferFree += buf->whdr.dwBufferLength;
    waveOutUnprepareHeader(device, &buf->whdr, sizeof(WAVEHDR));

    if (buf->starved && (buf->streamId == streamId)) {
      unsigned long long now = getTimestamp();
      if (now > buf->starvedAt) {
        // FILETIME 以 100 纳秒为单位计数
        unsigned long long ms = (now - buf->starvedAt + 9999) / 10000;
        if (ms > audioMaxJitterMs)
          ms = audioMaxJitterMs;
        if (extraDelayMs < ms)
          extraDelayMs = (unsigned)ms;
      }
    }
  }

  while (pendingSize > 0) {
    Buffer* buf;
    size_t length;

    length = pendingSize;
    if (length > bufferSize - writtenHead)
      length = bufferSize - writtenHead;

    if (spare != nullptr) {
      buf = spare;
      spare = buf->next;
    } else {
      buf = (Buffer*)malloc(sizeof(Buffer));
      if (buf == nullptr)
        break;
    }

    memset(buf, 0, sizeof(Buffer));
    buf->whdr.lpData = (LPSTR)(buffer + writtenHead);
    buf->whdr.dwBufferLength = (DWORD)length;
    buf->streamId = streamId;

    if (waveOutPrepareHeader(device, &buf->whdr,
                             sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
      buf->next = spare;
      spare = buf;
      break;
    }

    // 必须在写之前先计数，因为回调可能在 waveOutWrite() 还没返回
    // 之前就运行了
    InterlockedIncrement(&buffersInFlight);

    if (waveOutWrite(device, &buf->whdr,
                     sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
      InterlockedDecrement(&buffersInFlight);
      waveOutUnprepareHeader(device, &buf->whdr, sizeof(WAVEHDR));
      buf->next = spare;
      spare = buf;
      break;
    }

    writtenHead = (writtenHead + length) & (bufferSize - 1);
    pendingSize -= length;
  }

  while (spare != nullptr) {
    Buffer* next = spare->next;
    free(spare);
    spare = next;
  }
}

void AudioOutputWin32::start()
{
  if (!open())
    return;

  streamId++;

  // 先播一点静音，这样后到的样本比前一个晚时，设备也不会出现
  // 无东西可播的空档
  addSilence((audioMinStreamDelayMs + extraDelayMs) *
             audioFrequency / 1000);
  submit();
}

void AudioOutputWin32::stop()
{
  // 已经缓冲的东西仍然应该播完，所以这里只需让它排空即可
}

void AudioOutputWin32::play(const uint8_t* samples, size_t length)
{
  if (!opened)
    return;

  addSamples(samples, length);
  submit();
}
