/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 *
 * 本软件为自由软件；你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （GNU General Public License）的条款（第 2 版，或你选择的任何更高版本）
 * 重新分发和/或修改本软件。
 *
 * 本软件的发布是希望它有用，但不附带任何担保；甚至不附带适销性或
 * 特定用途适用性的默示担保。详见 GNU 通用公共许可证。
 *
 * 你应已收到随本软件一同提供的 GNU 通用公共许可证副本；如果没有，
 * 请致信自由软件基金会：Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA。
 */

// -=- Logger_file.cxx - 输出到文件的 Logger 实例

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <core/Logger_file.h>
#include <core/xdgdirs.h>

using namespace core;

Logger_File::Logger_File(const char* loggerName)
  : Logger(loggerName), indent(13), width(79),
    m_maxBytes(20 * 1024 * 1024), m_fileDay(0), m_file(nullptr),
    m_lastLogTime(0)
{
  m_filename[0] = '\0';
  m_logDir[0] = '\0';
}

Logger_File::~Logger_File()
{
  closeFile();
}

void Logger_File::write(int /*level*/, const char *logname, const char *message)
{
  if (m_logDir[0] != '\0') {
    // 滚动日志模式：跨日 / 超上限自动切新文件
    rotateIfNeeded();
    if (!m_file)
      return;
  } else if (!m_file) {
    // 固定文件名模式：首次写入时备份旧文件并新建
    if (m_filename[0] == '\0')
      return;
    char bakFilename[PATH_MAX];
    if (snprintf(bakFilename, sizeof(bakFilename),
                 "%s.bak", m_filename) >= (int)sizeof(bakFilename)) {
      remove(m_filename);
    } else {
      remove(bakFilename);
      rename(m_filename, bakFilename);
    }
    m_file = fopen(m_filename, "w+");
    if (!m_file) return;
  }

  time_t current = time(nullptr);
  if (current != m_lastLogTime) {
    m_lastLogTime = current;
    fprintf(m_file, "\n%s", ctime(&m_lastLogTime));
  }

  fprintf(m_file," %s:", logname);
  int column = strlen(logname) + 2;
  if (column < indent) {
    fprintf(m_file,"%*s",indent-column,"");
    column = indent;
  }
  while (true) {
    const char* s = strchr(message, ' ');
    int wordLen;
    if (s) wordLen = s-message;
    else wordLen = strlen(message);

    if (column + wordLen + 1 > width) {
      fprintf(m_file,"\n%*s",indent,"");
      column = indent;
    }
    fprintf(m_file," %.*s",wordLen,message);
    column += wordLen + 1;
    message += wordLen + 1;
    if (!s) break;
  }
  fprintf(m_file,"\n");
  fflush(m_file);
}

void Logger_File::rotateIfNeeded()
{
  time_t now = time(nullptr);

  struct tm tmNow;
  struct tm tmStart;
#ifdef WIN32
  localtime_s(&tmNow, &now);
  localtime_s(&tmStart, &m_fileDay);
#else
  localtime_r(&now, &tmNow);
  localtime_r(&m_fileDay, &tmStart);
#endif

  bool needRotate = (m_file == nullptr);

  if (!needRotate) {
    // 跨日：年份或一年中的第几天变化
    if (tmNow.tm_year != tmStart.tm_year || tmNow.tm_yday != tmStart.tm_yday)
      needRotate = true;
    // 超过单文件上限
    if (!needRotate) {
      struct stat st;
      if (stat(m_filename, &st) == 0 && st.st_size > m_maxBytes)
        needRotate = true;
    }
  }

  if (!needRotate)
    return;

  // 生成新文件名：目录/年月日时分秒.log
  char base[64];
  strftime(base, sizeof(base), "%Y%m%d%H%M%S.log", &tmNow);

  char newPath[PATH_MAX];
  if (snprintf(newPath, sizeof(newPath), "%s/%s", m_logDir, base)
      >= (int)sizeof(newPath))
    return;

  closeFile();
  snprintf(m_filename, sizeof(m_filename), "%s", newPath);
  m_fileDay = now;

  /* 确保日志目录存在（首次运行时安装目录下可能还没有 logs） */
  core::mkdir_p(m_logDir, 0755);

  m_file = fopen(m_filename, "w+");
}

void Logger_File::setLogDir(const char* dir, long maxBytes)
{
  closeFile();
  m_logDir[0] = '\0';
  if (dir && strlen(dir) < sizeof(m_logDir))
    strcpy(m_logDir, dir);
  m_maxBytes = (maxBytes > 0) ? maxBytes : (20L * 1024 * 1024);
}

const char* Logger_File::ensureRotated()
{
  if (m_logDir[0] == '\0')
    return nullptr;
  rotateIfNeeded();
  return m_filename;
}

void Logger_File::setFilename(const char* filename)
{
  closeFile();
  m_filename[0] = '\0';
  if (strlen(filename) >= sizeof(m_filename))
    return;
  strcpy(m_filename, filename);
}

void Logger_File::setFile(FILE* file)
{
  closeFile();
  m_file = file;
}

void Logger_File::closeFile()
{
  if (m_file) {
    fclose(m_file);
    m_file = nullptr;
  }
}

static Logger_File logger("file");

bool core::initFileLogger(const char* filename)
{
  logger.setFilename(filename);
  logger.registerLogger();
  return true;
}

const char* core::initRollingFileLogger(const char* dir, long maxBytes)
{
  logger.setLogDir(dir, maxBytes);
  logger.registerLogger();
  return logger.ensureRotated();
}
