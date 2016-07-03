#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

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

int tokenizeStats(int fd, TokenizerCb cb, void *userdata)
{
	bool doParse;
	const char *s;
	char strstat[1024];
	ssize_t readRet;
	const char *start = NULL;
	ParserState state;
	int idx = 0;
	int ret;

	if (fd == -1 || !cb)
		return -EINVAL;

	readRet = pread(fd, strstat, sizeof(strstat), 0);
	if (readRet == -1) {
		ret = -errno;
		printf("read() failed : %d(%m)", errno);
		return ret;
	}

	// Remove trailing '\n'
	strstat[readRet - 1] = '\0';

	s = strstat;
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
	int fd;
	int ret;

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);

	fd = open(path, O_RDONLY|O_CLOEXEC);
	if (fd == -1) {
		ret = -errno;
		printf("Fail to open %s : %d(%m)\n", path, errno);
		return ret;
	}

	testCtx.mName = name;
	testCtx.mMatch = false;

	ret = tokenizeStats(fd, pidTestCb, &testCtx);
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

int readProcessStats(int fd, SystemMonitor::ProcessStats *stats)
{
	if (!stats)
		return -EINVAL;

	return tokenizeStats(fd, processStatsCb, stats);
}

int readThreadStats(int fd, SystemMonitor::ThreadStats *stats)
{
	if (!stats)
			return -EINVAL;

	return tokenizeStats(fd, threadStatsCb, stats);
}

} // namespace pfstools
