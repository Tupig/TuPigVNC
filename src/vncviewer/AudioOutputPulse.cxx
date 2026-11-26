/* Copyright 2026 jose-pr
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

#include "AudioOutputPulse.h"

static core::LogWriter vlog("AudioOutputPulse");

// create() 必须在 viewer 连接任何东西之前就说明音频是否可用，所以
// 这段等待位于第一个窗口出现的路径上。本地 socket 另一端的声音服务
// 响应远低于 1 毫秒；完全不响应的服务不能把 viewer 卡住。
static const unsigned connectTimeoutMs = 500;

// 较宽的 RFB 格式采用主机字节序，而 PulseAudio 的 "NE" 格式正是
// 主机字节序，所以这套映射在大端机器上同样成立。有符号 8 位和
// 无符号 16 位没有对应格式，不过我们从不请求这两种。
static bool fillSampleSpec(pa_sample_spec* spec)
{
  switch (audioSampleFormat) {
  case rfb::qemuAudioFormatU8:
    spec->format = PA_SAMPLE_U8;
    break;
  case rfb::qemuAudioFormatS16:
    spec->format = PA_SAMPLE_S16NE;
    break;
  case rfb::qemuAudioFormatS32:
    spec->format = PA_SAMPLE_S32NE;
    break;
  default:
    return false;
  }

  spec->rate = audioFrequency;
  spec->channels = audioChannels;

  return true;
}

AudioOutputPulse::AudioOutputPulse()
  : available(false), opened(false), timedOut(false),
    mainloop(nullptr), context(nullptr), stream(nullptr),
    buffer(nullptr), bufferSize(0), bufferFree(0), pendingSize(0),
    writtenHead(0), pendingHead(0),
    streamId(0), extraDelayMs(0),
    starved(false), starvedAt(0), starvedStreamId(0),
    pendingError(nullptr), pendingErrorCode(0)
{
  pa_sample_spec spec;

  if (!fillSampleSpec(&spec))
    return;

  available = connect();
}

AudioOutputPulse::~AudioOutputPulse()
{
  // 拆解 stream 以及它正在读取的缓冲区时，mainloop 线程上不能有
  // 任何东西在跑
  if (mainloop != nullptr)
    pa_threaded_mainloop_stop(mainloop);

  if (stream != nullptr) {
    pa_stream_disconnect(stream);
    pa_stream_unref(stream);
  }

  if (context != nullptr) {
    pa_context_disconnect(context);
    pa_context_unref(context);
  }

  if (mainloop != nullptr)
    pa_threaded_mainloop_free(mainloop);

  free(buffer);
}

// 这里唯一会阻塞的就是连接，因为 create() 没法表达“稍后再问我”
bool AudioOutputPulse::connect()
{
  pa_mainloop_api* api;
  pa_proplist* proplist;
  pa_time_event* timeout;
  bool ready;

  mainloop = pa_threaded_mainloop_new();
  if (mainloop == nullptr)
    return false;

  if (pa_threaded_mainloop_start(mainloop) < 0) {
    pa_threaded_mainloop_free(mainloop);
    mainloop = nullptr;
    return false;
  }

  pa_threaded_mainloop_lock(mainloop);

  api = pa_threaded_mainloop_get_api(mainloop);

  // 应用 id 是 PulseAudio 用来查我们 .desktop 文件的依据，图标名则是
  // 它查不到时的兜底
  proplist = pa_proplist_new();
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "TuPigVNC");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "vncviewer");
  pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "tigervnc");

  context = pa_context_new_with_proplist(api, "TuPigVNC", proplist);

  pa_proplist_free(proplist);

  if (context == nullptr) {
    pa_threaded_mainloop_unlock(mainloop);
    return false;
  }

  pa_context_set_state_callback(context, contextStateCallback, this);

  // 启动一个没在运行的声音服务不是我们该干的事
  if (pa_context_connect(context, nullptr,
                         PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
    vlog.error(_("Could not connect to sound server: %s"),
               pa_strerror(pa_context_errno(context)));
    pa_threaded_mainloop_unlock(mainloop);
    return false;
  }

  // pa_threaded_mainloop_wait() 自身没有超时，所以如果服务端一直
  // 不回应，就得靠 loop 上的东西把我们唤醒
  timeout = pa_context_rttime_new(context,
                                  pa_rtclock_now() +
                                    connectTimeoutMs * PA_USEC_PER_MSEC,
                                  timeoutCallback, this);

  while (true) {
    pa_context_state_t state;

    state = pa_context_get_state(context);
    if (state == PA_CONTEXT_READY) {
      ready = true;
      break;
    }
    if (!PA_CONTEXT_IS_GOOD(state)) {
      vlog.error(_("Could not connect to sound server: %s"),
                 pa_strerror(pa_context_errno(context)));
      ready = false;
      break;
    }
    if (timedOut) {
      vlog.error(_("Timed out connecting to sound server"));
      ready = false;
      break;
    }

    pa_threaded_mainloop_wait(mainloop);
  }

  if (timeout != nullptr)
    api->time_free(timeout);

  pa_threaded_mainloop_unlock(mainloop);

  return ready;
}

size_t AudioOutputPulse::getSampleSize() const
{
  return audioChannels << (audioSampleFormat >> 1);
}

bool AudioOutputPulse::open()
{
  pa_sample_spec spec;
  pa_buffer_attr attr;
  size_t samples, sampleSize;

  if (opened)
    return true;
  if (!available)
    return false;

  fillSampleSpec(&spec);

  // 把样本数向上取整到 2 的幂，这样下面的回绕运算就能用掩码实现。
  // 样本大小同样是 2 的幂，所以字节数最终也是 2 的幂。
  samples = 1;
  while (samples < (4 * audioMaxJitterMs * audioFrequency) / 1000)
    samples <<= 1;

  sampleSize = getSampleSize();

  buffer = (uint8_t*)calloc(samples, sampleSize);
  if (buffer == nullptr) {
    available = false;
    return false;
  }

  bufferSize = bufferFree = samples * sampleSize;
  pendingSize = writtenHead = pendingHead = 0;

  pa_threaded_mainloop_lock(mainloop);

  stream = pa_stream_new(context, "Remote audio", &spec, nullptr);
  if (stream == nullptr) {
    vlog.error(_("Could not create audio playback stream: %s"),
               pa_strerror(pa_context_errno(context)));
    pa_threaded_mainloop_unlock(mainloop);
    free(buffer);
    buffer = nullptr;
    available = false;
    return false;
  }

  pa_stream_set_state_callback(stream, streamStateCallback, this);
  pa_stream_set_write_callback(stream, streamWriteCallback, this);
  pa_stream_set_underflow_callback(stream, streamUnderflowCallback, this);

  // 服务端该缓冲多少，以及它低于多少就开始播放。前者决定了我们最多
  // 能提前写多少，这样积压会留在我们自己的缓冲区里被度量、被丢弃，
  // 而不是在库内部越堆越大。后者让流能迅速开始，并能从欠载中恢复，
  // 不再额外产生一段空档。
  memset(&attr, 0, sizeof(attr));
  attr.maxlength = (uint32_t)-1;
  attr.tlength = (uint32_t)(audioMaxJitterMs * audioFrequency /
                            1000 * sampleSize);
  attr.prebuf = (uint32_t)(audioMinStreamDelayMs * audioFrequency /
                           1000 * sampleSize);
  attr.minreq = (uint32_t)-1;
  attr.fragsize = (uint32_t)-1;

  if (pa_stream_connect_playback(stream, nullptr, &attr,
                                 PA_STREAM_NOFLAGS, nullptr, nullptr) < 0) {
    vlog.error(_("Could not open audio playback device: %s"),
               pa_strerror(pa_context_errno(context)));
    pa_stream_unref(stream);
    stream = nullptr;
    pa_threaded_mainloop_unlock(mainloop);
    free(buffer);
    buffer = nullptr;
    available = false;
    return false;
  }

  // 这里故意不等流就绪。在此期间交给我们的样本会放进缓冲区，等有
  // 地方可写时由状态回调把它们写出。
  opened = true;

  pa_threaded_mainloop_unlock(mainloop);

  return true;
}

void AudioOutputPulse::addSilence(size_t samples)
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

void AudioOutputPulse::addSamples(const uint8_t* samples, size_t length)
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

// 把缓冲区里服务端当前想要的部分尽量多交给它。调用时必须持有
// mainloop 锁，这也正是它能安全地从 mainloop 自己的回调里调用的
// 原因。
void AudioOutputPulse::submit()
{
  if (!opened)
    return;
  if (pa_stream_get_state(stream) != PA_STREAM_READY)
    return;
  if (pendingSize == 0)
    return;

  // 服务端断流之后又有东西可交，正是它告诉我们断流持续了多久，
  // 也就是我们还需要多提前缓冲多少
  if (starved) {
    starved = false;
    if (starvedStreamId == streamId) {
      unsigned long long now = pa_rtclock_now();
      if (now > starvedAt) {
        unsigned long long ms;
        ms = (now - starvedAt + PA_USEC_PER_MSEC - 1) / PA_USEC_PER_MSEC;
        if (ms > audioMaxJitterMs)
          ms = audioMaxJitterMs;
        if (extraDelayMs < ms)
          extraDelayMs = (unsigned)ms;
      }
    }
  }

  while (pendingSize > 0) {
    size_t writable, length;

    // 写入超过服务端想要的数量，只会把积压挪进库内部，那里没有任何
    // 东西给它设上限
    writable = pa_stream_writable_size(stream);
    if (writable == (size_t)-1)
      break;

    length = pendingSize;
    if (length > bufferSize - writtenHead)
      length = bufferSize - writtenHead;
    if (length > writable)
      length = writable;

    // 绝不把一个样本拆到两次写入里
    length -= length % getSampleSize();
    if (length == 0)
      break;

    if (pa_stream_write(stream, buffer + writtenHead, length,
                        nullptr, 0, PA_SEEK_RELATIVE) < 0) {
      setError(N_("Could not write to audio playback device"));
      break;
    }

    // 样本已经拷走，这块空间立刻又归我们了
    writtenHead = (writtenHead + length) & (bufferSize - 1);
    pendingSize -= length;
    bufferFree += length;
  }
}

// 记录在 mainloop 线程上发现的错误。该线程上除了 PulseAudio 之外的
// 东西都不能碰，包括打日志，所以由主线程稍后在 reportError() 里上报。
void AudioOutputPulse::setError(const char* message)
{
  if (pendingError != nullptr)
    return;

  pendingError = message;
  pendingErrorCode = pa_context_errno(context);
}

// 必须从主线程调用，且不能持有 mainloop 锁
void AudioOutputPulse::reportError()
{
  const char* message;
  int code;

  if (mainloop == nullptr)
    return;

  pa_threaded_mainloop_lock(mainloop);
  message = pendingError;
  code = pendingErrorCode;
  pendingError = nullptr;
  pa_threaded_mainloop_unlock(mainloop);

  if (message == nullptr)
    return;

  vlog.error("%s: %s", _(message), pa_strerror(code));
}

// 在 mainloop 自己的线程上调用，且持有它的锁，所以缓冲区和所有受
// 该锁保护的东西都可以直接访问
void AudioOutputPulse::contextStateCallback(pa_context* context,
                                            void* userdata)
{
  AudioOutputPulse* self = (AudioOutputPulse*)userdata;

  // 只有 FAILED 值得专门说：在我们变为可用之前，这只是上面那个
  // connect 在推进，失败由 connect() 自己上报；而 TERMINATED 是我们
  // 自己断开，pa_context_disconnect() 会在这里直接回调告诉我们
  if (self->available &&
      (pa_context_get_state(context) == PA_CONTEXT_FAILED))
    self->setError(N_("Lost connection to sound server"));

  pa_threaded_mainloop_signal(self->mainloop, 0);
}

void AudioOutputPulse::streamStateCallback(pa_stream* stream, void* userdata)
{
  AudioOutputPulse* self = (AudioOutputPulse*)userdata;

  switch (pa_stream_get_state(stream)) {
  case PA_STREAM_READY:
    // 流还在连接期间缓冲下来的东西，现在都可以发出去了
    self->submit();
    break;
  case PA_STREAM_FAILED:
    self->setError(N_("Audio playback stream failed"));
    break;
  default:
    break;
  }
}

void AudioOutputPulse::streamWriteCallback(pa_stream* /*stream*/,
                                           size_t /*length*/, void* userdata)
{
  AudioOutputPulse* self = (AudioOutputPulse*)userdata;

  self->submit();
}

void AudioOutputPulse::streamUnderflowCallback(pa_stream* /*stream*/,
                                               void* userdata)
{
  AudioOutputPulse* self = (AudioOutputPulse*)userdata;

  // 没有东西可播，说明服务端在等我们时断流了。它断流多久会在
  // submit() 里算出来，也就是我们终于有东西可给它的那一刻。
  if (!self->starved) {
    self->starved = true;
    self->starvedAt = pa_rtclock_now();
    self->starvedStreamId = self->streamId;
  }
}

void AudioOutputPulse::timeoutCallback(pa_mainloop_api* /*api*/,
                                       pa_time_event* /*event*/,
                                       const struct timeval* /*tv*/,
                                       void* userdata)
{
  AudioOutputPulse* self = (AudioOutputPulse*)userdata;

  self->timedOut = true;
  pa_threaded_mainloop_signal(self->mainloop, 0);
}

void AudioOutputPulse::start()
{
  if (!open())
    return;

  pa_threaded_mainloop_lock(mainloop);

  streamId++;

  // 先播一点静音，这样后到的样本比前一个晚时，设备也不会出现
  // 无东西可播的空档
  addSilence((audioMinStreamDelayMs + extraDelayMs) *
             audioFrequency / 1000);
  submit();

  pa_threaded_mainloop_unlock(mainloop);

  reportError();
}

void AudioOutputPulse::stop()
{
  // 已经缓冲的东西仍然应该播完，所以这里只需让它排空即可
  reportError();
}

void AudioOutputPulse::play(const uint8_t* samples, size_t length)
{
  if (!opened)
    return;

  pa_threaded_mainloop_lock(mainloop);

  addSamples(samples, length);
  submit();

  pa_threaded_mainloop_unlock(mainloop);

  reportError();
}
