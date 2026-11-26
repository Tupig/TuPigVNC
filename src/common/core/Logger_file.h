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

// -=- Logger_file - 将日志写入文件

#ifndef __CORE_LOGGER_FILE_H__
#define __CORE_LOGGER_FILE_H__

#include <time.h>
#include <limits.h>

#include <core/Logger.h>

namespace core {

  class Logger_File : public Logger {
  public:
    Logger_File(const char* loggerName);
    ~Logger_File();

    void write(int level, const char *logname, const char *message) override;
    void setFilename(const char* filename);
    void setFile(FILE* file);

    /*
     * 滚动日志：设置日志目录和单文件上限（字节）。
     * 写入时若「跨日」或「超过上限」，自动以「年月日时分秒.log」
     * 新建文件，保证单个日志文件不会无限膨胀。
     * maxBytes <= 0 时使用默认 20MB。
     */
    void setLogDir(const char* dir, long maxBytes);

    /* 立即确保日志文件已创建（启动时调用），返回当前文件路径 */
    const char* ensureRotated();

    int indent;
    int width;

  protected:
    void closeFile();
    void rotateIfNeeded();

    char m_filename[PATH_MAX];
    char m_logDir[PATH_MAX];
    long m_maxBytes;
    time_t m_fileDay;
    FILE* m_file;
    time_t m_lastLogTime;
  };

  bool initFileLogger(const char* filename);

  /*
   * 启动滚动文件日志。dir 为日志目录（不存在会自动创建），
   * maxBytes 为单文件上限。返回实际日志文件路径，失败返回 nullptr。
   * 调用后需再 setLogParams("*:file:<level>") 把日志导向 "file"。
   */
  const char* initRollingFileLogger(const char* dir, long maxBytes);
}

#endif
