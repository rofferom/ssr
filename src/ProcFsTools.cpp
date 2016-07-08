#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include "ProcFsTools.hpp"

namespace {

typedef bool (*TokenizerCb) (
		int idx,
		const char *start,
		const char *end,
		void *userdata);

enum ProcStatIdx {
	STAT_IDX_PID = 0,
	STAT_IDX_NAME = 1,
	STAT_IDX_UTIME = 13,
	STAT_IDX_STIME = 14,
	STAT_IDX_THREADCOUNT= 19,
	STAT_IDX_VSIZE = 22,
	STAT_IDX_RSS = 23
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
	case STAT_IDX_PID:
		stats->mPid = atoi(buf);
		break;

	case STAT_IDX_NAME:
		strncpy(stats->mName, buf, sizeof(stats->mName));
		break;

	case STAT_IDX_UTIME:
		stats->mUtime = atoll(buf);
		break;

	case STAT_IDX_STIME:
		stats->mStime = atoll(buf);
		break;

	case STAT_IDX_THREADCOUNT:
		stats->mThreadCount = atoi(buf);
		break;

	case STAT_IDX_VSIZE:
		stats->mVsize = atoi(buf);
		break;

	case STAT_IDX_RSS:
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
	case STAT_IDX_PID:
		stats->mTid = atoi(buf);
		break;

	case STAT_IDX_NAME:
		strncpy(stats->mName, buf, sizeof(stats->mName));
		break;

	case STAT_IDX_UTIME:
		stats->mUtime = atoll(buf);
		break;

	case STAT_IDX_STIME:
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

	if (idx != STAT_IDX_NAME)
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

} // anonymous namespace

namespace pfstools {

int findProcess(const char *name, int *outPid)
{
	DIR *d;
	struct dirent entry;
	struct dirent *result = nullptr;
	int pid;
	char *endptr;
	int ret;
	bool found = false;

	d = opendir("/proc");
	if (!d) {
		ret = -errno;
		printf("Fail to open /proc : %d(%m)\n", errno);
		return ret;
	}

	while (!found) {
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

		found = testPidName(pid, name);
	}

	closedir(d);

	if (found)
		*outPid = pid;

	return found ? 0 : -ENOENT;
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
