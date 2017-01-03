#ifndef __LOG_HPP__
#define __LOG_HPP__

#include <stdint.h>
#include <stdarg.h>
#include <string.h>

enum LogLevel {
	LOG_CRIT,
	LOG_ERR,
	LOG_WARN,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG
};

#define LOGC(...) LOG_PRI(LOG_CRIT,   __VA_ARGS__)
#define LOGE(...) LOG_PRI(LOG_ERR,    __VA_ARGS__)
#define LOGW(...) LOG_PRI(LOG_WARN,   __VA_ARGS__)
#define LOGN(...) LOG_PRI(LOG_NOTICE, __VA_ARGS__)
#define LOGI(...) LOG_PRI(LOG_INFO,   __VA_ARGS__)
#define LOGD(...) LOG_PRI(LOG_DEBUG,  __VA_ARGS__)

#define LOG_PRI(_prio, ...)  __log(_prio, __VA_ARGS__)

#define LOG_ERRNO(_func) \
	LOGE("%s:%d %s err=%d(%s)", __func__, __LINE__, \
			_func, errno, strerror(errno))

typedef void (*log_cb_t) (uint32_t prio, const char *fmt, va_list ap);

void __log(uint32_t prio, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

void logSetCb(log_cb_t cb);

void logSetLevel(uint32_t prio);

#endif // !__LOG_HPP__
