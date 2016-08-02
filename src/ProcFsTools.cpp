#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "ProcFsTools.hpp"

#define SIZEOF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

namespace {

typedef bool (*TokenizerCb) (
		int idx,
		const char *start,
		const char *end,
		void *userdata);

typedef bool (*PidFoundCb) (int pid, void *userdata);

enum SysStatLine {
	SYSSTAT_UNKNOWN,
	SYSSTAT_LINE_CPU,
	SYSSTAT_IRQ,
	SYSSTAT_SOFT_IRQ,
	SYSSTAT_CTX_SWITCH,
};

enum SysStatCpuIdx {
	SYSSTAT_CPU_IDX_USER = 1,
	SYSSTAT_CPU_IDX_NICE = 2,
	SYSSTAT_CPU_IDX_SYSTEM = 3,
	SYSSTAT_CPU_IDX_IDLE = 4,
	SYSSTAT_CPU_IDX_IOWAIT = 5,
	SYSSTAT_CPU_IDX_IRQ = 6,
	SYSSTAT_CPU_IDX_SOFTIRQ = 7,
};

enum SysStatIrqIdx {
	SYSSTAT_IRQ_IDX_COUNT = 1
};

enum SysStatSoftIrqIdx {
	SYSSTAT_SOFTIRQ_IDX_COUNT = 1
};

enum SysStatCtxSwitchIdx {
	SYSSTAT_CTXSWITCH_COUNT = 1
};

enum ProcStatIdx {
	PROCSTAT_IDX_PID = 0,
	PROCSTAT_IDX_NAME = 1,
	PROCSTAT_IDX_UTIME = 13,
	PROCSTAT_IDX_STIME = 14,
	PROCSTAT_IDX_THREADCOUNT= 19,
	PROCSTAT_IDX_VSIZE = 22,
	PROCSTAT_IDX_RSS = 23
};

enum ParserState {
	PARSER_STATE_IDLE = 0,
	PARSER_STATE_INT,
	PARSER_STATE_STR
};

struct PidTestCtx {
	const char *mName;
	bool mMatch;
};

int tokenizeStats(const char *s, TokenizerCb cb, void *userdata)
{
	bool doParse;
	const char *start = NULL;
	ParserState state;
	int idx = 0;

	if (!s|| !cb)
		return -EINVAL;

	state = PARSER_STATE_IDLE;
	while (*s != '\0') {
		switch (state) {
		case PARSER_STATE_IDLE:
			if (*s == '(') {
				state = PARSER_STATE_STR;
				start = s;
			} else if (*s != ' ') {
				state = PARSER_STATE_INT;
				start = s;
			}
			break;

		case PARSER_STATE_INT:
			if (*s == ' ') {
				doParse = cb(idx, start, s - 1, userdata);
				if (!doParse)
					return 0;

				state = PARSER_STATE_IDLE;
				idx++;
			}
			break;

		case PARSER_STATE_STR:
			if (*s == ')') {
				doParse = cb(idx, start, s, userdata);
				if (!doParse)
					return 0;

				state = PARSER_STATE_IDLE;
				idx++;
			}
			break;

		default:
			break;
		}

		s++;
	}

	switch (state) {
	case PARSER_STATE_INT:
		cb(idx, start, s - 1, userdata);
		break;

	case PARSER_STATE_STR:
		cb(idx, start, s - 1, userdata);
		break;

	default:
		break;
	}

	return 0;
}

bool processStatsCb(int idx,
		     const char *start,
		     const char *end,
		     void *userdata)
{
	SystemMonitor::ProcessStats *stats = (SystemMonitor::ProcessStats *) userdata;
	char buf[64];
	bool ret = true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	switch (idx) {
	case PROCSTAT_IDX_PID:
		stats->mPid = atoi(buf);
		break;

	case PROCSTAT_IDX_NAME:
		strncpy(stats->mName, buf, sizeof(stats->mName));
		break;

	case PROCSTAT_IDX_UTIME:
		stats->mUtime = atoll(buf);
		break;

	case PROCSTAT_IDX_STIME:
		stats->mStime = atoll(buf);
		break;

	case PROCSTAT_IDX_THREADCOUNT:
		stats->mThreadCount = atoi(buf);
		break;

	case PROCSTAT_IDX_VSIZE:
		stats->mVsize = atoi(buf);
		break;

	case PROCSTAT_IDX_RSS:
		stats->mRss = atoi(buf);
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

bool threadStatsCb(int idx,
		   const char *start,
		   const char *end,
		   void *userdata)
{
	SystemMonitor::ThreadStats *stats = (SystemMonitor::ThreadStats *) userdata;
	char buf[64];
	bool ret = true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	switch (idx) {
	case PROCSTAT_IDX_PID:
		stats->mTid = atoi(buf);
		break;

	case PROCSTAT_IDX_NAME:
		strncpy(stats->mName, buf, sizeof(stats->mName));
		break;

	case PROCSTAT_IDX_UTIME:
		stats->mUtime = atoll(buf);
		break;

	case PROCSTAT_IDX_STIME:
		stats->mStime = atoll(buf);
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

bool pidTestCb(int idx,
		      const char *start,
		      const char *end,
		      void *userdata)
{
	PidTestCtx *ctx = (PidTestCtx *) userdata;
	char buf[64];

	if (idx != PROCSTAT_IDX_NAME)
		return true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	ctx->mMatch = (strncmp(buf + 1, ctx->mName, strlen(buf) - 2) == 0);

	return false;
}

bool testPidName(int pid, const char *name)
{
	PidTestCtx testCtx;
	char path[128];
	pfstools::RawStats rawStats;
	int fd;
	int ret;

	// Open procfs
	snprintf(path, sizeof(path), "/proc/%d/stat", pid);

	fd = open(path, O_RDONLY|O_CLOEXEC);
	if (fd == -1) {
		ret = -errno;
		printf("Fail to open %s : %d(%m)\n", path, errno);
		return ret;
	}

	// Read content
	ret = pfstools::readRawStats(fd, &rawStats);
	if (ret < 0) {
		close(fd);
		return false;
	}

	testCtx.mName = name;
	testCtx.mMatch = false;

	ret = tokenizeStats(rawStats.mContent, pidTestCb, &testCtx);
	close(fd);
	if (ret < 0) {
		return false;
	}

	return testCtx.mMatch;
}

bool processCpuCb(int idx,
		  const char *start,
		  const char *end,
		  void *userdata)
{
	auto stats = (SystemMonitor::SystemStats *) userdata;
	char buf[64];
	bool ret = true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	switch (idx) {
	case SYSSTAT_CPU_IDX_USER:
		stats->mUtime = atoi(buf);
		break;

	case SYSSTAT_CPU_IDX_NICE:
		stats->mNice = atoi(buf);
		break;

	case SYSSTAT_CPU_IDX_SYSTEM:
		stats->mStime = atoi(buf);
		break;

	case SYSSTAT_CPU_IDX_IDLE:
		stats->mIdle = atoi(buf);
		break;

	case SYSSTAT_CPU_IDX_IOWAIT:
		stats->mIoWait = atoi(buf);
		break;

	case SYSSTAT_CPU_IDX_IRQ:
		stats->mIrq = atoi(buf);
		break;

	case SYSSTAT_CPU_IDX_SOFTIRQ:
		stats->mSoftIrq = atoi(buf);
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

bool processIrqCountCb(int idx,
		       const char *start,
		       const char *end,
		       void *userdata)
{
	auto stats = (SystemMonitor::SystemStats *) userdata;
	char buf[64];
	bool ret = true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	switch (idx) {
	case SYSSTAT_IRQ_IDX_COUNT:
		stats->mIrqCount = atoi(buf);
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

bool processSoftIrqCountCb(int idx,
			   const char *start,
			   const char *end,
			   void *userdata)
{
	auto stats = (SystemMonitor::SystemStats *) userdata;
	char buf[64];
	bool ret = true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	switch (idx) {
	case SYSSTAT_SOFTIRQ_IDX_COUNT:
		stats->mSoftIrqCount = atoi(buf);
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

bool processCtxSwitchCountCb(int idx,
			     const char *start,
			     const char *end,
			     void *userdata)
{
	auto stats = (SystemMonitor::SystemStats *) userdata;
	char buf[64];
	bool ret = true;

	if ((size_t) (end - start + 1) > sizeof(buf))
		return false;

	memcpy(buf, start, end - start + 1);
	buf[end - start + 1] = '\0';

	switch (idx) {
	case SYSSTAT_CTXSWITCH_COUNT:
		stats->mCtxSwitchCount = atoi(buf);
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

int iterateAllPid(PidFoundCb cb, void *userdata)
{
	DIR *d;
	struct dirent entry;
	struct dirent *result = nullptr;
	int pid;
	char *endptr;
	int ret;
	bool process = true;

	if (!cb)
		return -EINVAL;

	d = opendir("/proc");
	if (!d) {
		ret = -errno;
		printf("Fail to open /proc : %d(%m)\n", errno);
		return ret;
	}

	while (process) {
		ret = readdir_r(d, &entry, &result);
		if (ret == 0 && !result)
			break;

		pid = strtol(entry.d_name, &endptr, 10);
		if (errno == ERANGE) {
			printf("Ignore %s\n", entry.d_name);
			continue;
		} else if (*endptr != '\0') {
			continue;
		}

		process = cb(pid, userdata);
	}

	closedir(d);

	return 0;
}

} // anonymous namespace

namespace pfstools {

struct FindProcessCtx {
	const char *mName;
	int mPid;
};

static bool findProcessCb(int pid, void *userdata)
{
	auto ctx = (FindProcessCtx *) userdata;
	bool match;

	match = testPidName(pid, ctx->mName);
	if (match) {
		ctx->mPid = pid;
		return false;
	}

	return true;
}

int findProcess(const char *name, int *outPid)
{
	FindProcessCtx ctx;
	int ret;

	ctx.mName = name;
	ctx.mPid = -1;

	ret = iterateAllPid(findProcessCb, &ctx);
	if (ret < 0)
		return ret;

	if (ctx.mPid != -1) {
		ret = 0;
		*outPid = ctx.mPid;
	} else {
		ret = -ENOENT;
	}

	return ret;
}

static bool findAllProcessesCb(int pid, void *userdata)
{
	auto pidList = (std::list<int> *) userdata;

	pidList->push_back(pid);

	return true;
}

int findAllProcesses(std::list<int> *outPid)
{
	int ret;

	ret = iterateAllPid(findAllProcessesCb, outPid);
	if (ret < 0)
		return ret;

	return 0;
}

static int getTimeNs(uint64_t *ns)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		ret = -errno;
		printf("clock_gettime() failed : %d(%m)", errno);
		return ret;
	}

	*ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

	return 0;
}

int readRawStats(int fd, RawStats *stats)
{
	ssize_t readRet;
	int ret;

	if (fd == -1 || !stats)
		return -EINVAL;

	getTimeNs(&stats->mTs);
	readRet = pread(fd, stats->mContent, sizeof(stats->mContent), 0);
	getTimeNs(&stats->mAcqEnd);
	if (readRet == -1) {
		ret = -errno;
		stats->mPending = false;
		printf("read() failed : %d(%m)", errno);
		return ret;
	}

	// Remove trailing '\n'
	stats->mContent[readRet - 1] = '\0';
	stats->mPending = true;

	return 0;
}

static int getNextLine(char *s, char **end, bool *endOfString)
{
	while (true) {
		if (*s == '\n') {
			*s = '\0';
			*end = s;
			*endOfString = false;
			break;
		} else if (*s == '\0') {
			*end = s;
			*endOfString = true;
			break;
		} else {
			s++;
		}
	}

	return 0;
}

static SysStatLine getSystemStatsLine(const char *s)
{
	const char *end;

	struct {
		const char *s;
		SysStatLine v;
	} values[] = {
		{ "cpu",     SYSSTAT_LINE_CPU },
		{ "intr",    SYSSTAT_IRQ },
		{ "softirq", SYSSTAT_SOFT_IRQ },
		{ "ctxt",    SYSSTAT_CTX_SWITCH }
	};

	end = strchr(s, ' ');
	if (!end)
		return SYSSTAT_UNKNOWN;

	for (size_t i = 0; i < SIZEOF_ARRAY(values); i++) {
		if (strncmp(s, values[i].s, end - s) == 0)
			return values[i].v;
	}

	return SYSSTAT_UNKNOWN;
}

int readSystemStats(char *s, SystemMonitor::SystemStats *stats)
{
	TokenizerCb cb;
	SysStatLine lineType;
	bool endOfString = false;
	bool process = true;
	char *end;
	int ret;

	if (!s || !stats)
		return -EINVAL;

	for (int line = 0; process && !endOfString; line++) {
		ret = getNextLine(s, &end, &endOfString);
		if (ret < 0)
			return ret;

		lineType = getSystemStatsLine(s);
		cb = nullptr;
		switch (lineType) {
		case SYSSTAT_LINE_CPU:
			cb = processCpuCb;
			break;

		case SYSSTAT_IRQ:
			cb = processIrqCountCb;
			break;

		case SYSSTAT_SOFT_IRQ:
			cb = processSoftIrqCountCb;
			break;

		case SYSSTAT_CTX_SWITCH:
			cb = processCtxSwitchCountCb;
			break;

		default:
			break;
		}

		if (cb) {
			ret = tokenizeStats(s, cb, stats);
			if (ret < 0)
				return ret;
		}

		s = end + 1;
	}

	return 0;
}

int readProcessStats(const char *s, SystemMonitor::ProcessStats *stats)
{
	if (!stats)
		return -EINVAL;

	return tokenizeStats(s, processStatsCb, stats);
}

int readThreadStats(const char *s, SystemMonitor::ThreadStats *stats)
{
	if (!stats)
			return -EINVAL;

	return tokenizeStats(s, threadStatsCb, stats);
}

} // namespace pfstools
