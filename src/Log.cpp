#include <stdio.h>
#include "Log.hpp"

static uint32_t sLogLevel = LOG_INFO;

static void logCbDefault(uint32_t prio, const char *fmt, va_list ap)
{
	char buf[128];
	FILE *stream;
	char strPrio;

	if (prio > sLogLevel)
		return;

	switch (prio) {
	case LOG_CRIT:
		strPrio = 'C';
		stream = stderr;
		break;

	case LOG_ERR:
		strPrio = 'E';
		stream = stderr;
		break;

	case LOG_WARN:
		strPrio = 'W';
		stream = stderr;
		break;

	case LOG_NOTICE:
		strPrio = 'N';
		stream = stdout;
		break;

	case LOG_INFO:
		strPrio = 'I';
		stream = stdout;
		break;

	case LOG_DEBUG:
		strPrio = 'D';
		stream = stdout;
		break;

	default:
		strPrio = 'C';
		stream = stderr;
		break;
	}

	vsnprintf(buf, sizeof(buf), fmt, ap);
	fprintf(stream, "[%c] %s\n", strPrio, buf);
}

static log_cb_t sCb = logCbDefault;

void __log(uint32_t prio, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	sCb(prio, fmt, ap);
	va_end(ap);
}

void logSetCb(log_cb_t cb)
{
	if (!cb)
		sCb = logCbDefault;
	else
		sCb = cb;
}

void logSetLevel(uint32_t prio)
{
	sLogLevel = prio;
}
