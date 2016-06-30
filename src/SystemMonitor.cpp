#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include <string>
#include <list>
#include <map>

#include "SystemMonitor.hpp"

namespace {

#define INVALID_PID -1

#define STAT_PATTERN \
	"%d %128s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld "\
	"%ld %ld %llu %lu %ld"

#define STAT_PATTERN_COUNT 24

struct RawStats {
	int pid;
	char name[128];
	char state;
	int ppid;
	int pgrp;
	int session;
	int tty_nr;
	int tpgid;
	unsigned int flags;
	unsigned long int minflt;
	unsigned long int cminflt;
	unsigned long int majflt;
	unsigned long int cmajflt;
	unsigned long int utime;
	unsigned long int stime;
	long int cutime;
	long int cstime;
	long int priority;
	long int nice;
	long int num_threads;
	long int itrealvalue;
	unsigned long long int starttime;
	unsigned long int vsize;
	long int rss;

	RawStats()
	{
		pid = 0;
		name[0] = '\0';
		state = 0;
		ppid = 0;
		pgrp = 0;
		session = 0;
		tty_nr = 0;
		tpgid = 0;
		flags = 0;
		minflt = 0;
		cminflt = 0;
		majflt = 0;
		cmajflt = 0;
		utime = 0;
		stime = 0;
		cutime = 0;
		cstime = 0;
		priority = 0;
		nice = 0;
		num_threads = 0;
		itrealvalue = 0;
		starttime = 0;
		vsize = 0;
		rss = 0;
	}
};

struct SystemSettings {
	int mClkTck;
	int mPagesize ;
};

class ProcessMonitor {
private:
	struct ThreadInfo {
		int mTid;
		int mFd;;
		char mName[64];
		RawStats mPrevStats;
	};

private:
	int mStatFd;
	int mPid;
	std::string mName;
	RawStats mPrevStats;
	const SystemSettings *mSysSettings;

	std::map<int, ThreadInfo> mThreads;

private:
	void clear();

	int getPidFdCount();

	static int readStats(int fd, int pid, RawStats *procstat);
	static bool testPidName(int pid, const char *name);
	static int findProcess(const char *name, int *outPid);

	static uint16_t getCpuLoad(const RawStats &prevStats,
				   const RawStats &curStats,
				   const SystemSettings *sysSettings,
				   uint64_t timeDiffUs);


	int processThread(ThreadInfo *info,
			  uint64_t ts,
			  int timeDiff,
			  const SystemMonitor::Callbacks &cb);

	int addThread(int tid);

	int researchThreads();

	int processThreads(uint64_t ts,
			   int timeDiff,
			   long int numThreads,
			   const SystemMonitor::Callbacks &cb);

public:
	ProcessMonitor(const char *name, const SystemSettings *sysSettings);
	~ProcessMonitor();

	int process(uint64_t ts,
		    uint64_t timeDiff,
		    const SystemMonitor::Callbacks &cb);
};

ProcessMonitor::ProcessMonitor(const char *name,
			       const SystemSettings *sysSettings)
{
	mStatFd = -1;
	mName = name;
	mPid = INVALID_PID;
	mSysSettings = sysSettings;
}

ProcessMonitor::~ProcessMonitor()
{
	clear();
}

void ProcessMonitor::clear()
{
	if (mStatFd != -1) {
		close(mStatFd);
		mStatFd = -1;
	}

	mPid = INVALID_PID;

	for (auto p : mThreads)
		close(p.second.mFd);

	mThreads.clear();
}

uint16_t ProcessMonitor::getCpuLoad(const RawStats &prevStats,
				    const RawStats &curStats,
				    const SystemSettings *sysSettings,
				    uint64_t timeDiffUs)
{
	uint64_t ticks;
	double cpuload;

	ticks  = curStats.utime + curStats.stime;
	ticks -= prevStats.utime + prevStats.stime;

	cpuload  = (double) ticks / (double) sysSettings->mClkTck;
	cpuload /= (double) timeDiffUs / 1000000.0;

	return cpuload * 100;
}

int ProcessMonitor::processThread(ThreadInfo *info,
				  uint64_t ts,
				  int timeDiff,
				  const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::ThreadStats stats;
	RawStats rawStats;
	int ret;

	ret = readStats(info->mFd, info->mTid, &rawStats);
	if (ret < 0) {
		// Thread finished
		close(info->mFd);
		mThreads.erase(info->mTid);
		return ret;
	}

	stats.mTs = ts;
	stats.mPid = mPid;
	stats.mTid = info->mTid;
	stats.mName = info->mName;
	stats.mUtime = rawStats.utime;
	stats.mStime = rawStats.stime;

	stats.mCpuLoad = getCpuLoad(info->mPrevStats,
				    rawStats,
				    mSysSettings,
				    timeDiff);

	info->mPrevStats = rawStats;

	if (cb.mThreadStats)
		cb.mThreadStats(stats);

	return 0;
}

int ProcessMonitor::addThread(int tid)
{
	ThreadInfo info;
	char path[128];
	int ret;

	// Fill thread info
	snprintf(path, sizeof(path),
		 "/proc/%d/task/%d/stat",
		 mPid, tid);

	ret = open(path, O_RDONLY|O_CLOEXEC);
	if (ret == -1) {
		ret = -errno;
		printf("Fail to open %s : %d(%m)\n", path, errno);
		return ret;
	}

	info.mTid = tid;
	info.mFd = ret;

	ret = readStats(info.mFd, tid, &info.mPrevStats);
	if (ret < 0) {
		close(info.mFd);
		return ret;
	}

	snprintf(info.mName, sizeof(info.mName),
		 "%d-%s",
		 tid,
		 info.mPrevStats.name);

	// Register thread
	auto insertRet = mThreads.insert( {tid, info} );
	if (!insertRet.second) {
		printf("Fail to insert thread %d\n", tid);
		close(info.mFd);
		return -EPERM;
	}

	return 0;
}

int ProcessMonitor::researchThreads()
{
	DIR *d;
	struct dirent entry;
	struct dirent *result = nullptr;
	char path[128];
	int tid;
	char *endptr;
	int ret;

	snprintf(path, sizeof(path), "/proc/%d/task", mPid);

	d = opendir(path);
	if (!d) {
		ret = -errno;
		printf("Fail to open /proc : %d(%m)\n", errno);
		return ret;
	}

	while (true) {
		ret = readdir_r(d, &entry, &result);
		if (ret > 0) {
			printf("readdir_r() failed : %d(%s)\n",
			       ret, strerror(ret));
			ret = -ret;
			goto closedir;
		} else if (ret == 0 && !result) {
			break;
		}

		tid = strtol(entry.d_name, &endptr, 10);
		if (errno == ERANGE) {
			printf("Ignore %s\n", entry.d_name);
			continue;
		} else if (*endptr != '\0') {
			continue;
		}

		// Avoid to add thread if already exists
		if (mThreads.count(tid) == 0) {
			ret = addThread(tid);
			if (ret < 0)
				printf("Fail to add thread %d\n", tid);
		}
	}

	closedir(d);

	return 0;

closedir:
	closedir(d);

	return ret;
}

int ProcessMonitor::processThreads(uint64_t ts,
				   int timeDiff,
				   long int numThreads,
				   const SystemMonitor::Callbacks &cb)
{
	long int foundThreads;
	int ret;

	// Process all already known threads.
	foundThreads = 0;
	for (auto &thread : mThreads) {
		ret = processThread(&thread.second, ts, timeDiff, cb);
		if (ret < 0)
			printf("Fail to process thread %d\n", thread.first);
		else
			foundThreads++;
	}

	/**
	 * If there are missing threads, research any new thread.
	 * This can happen if :
	 * - a new thread has been created
	 * - a known thread has stopped
	 */
	if (foundThreads != numThreads)
		researchThreads();

	return 0;
}

int ProcessMonitor::process(uint64_t ts,
			    uint64_t timeDiff,
			    const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::ProcessStats stats;
	RawStats rawStats;
	char path[64];
	int ret;

	if (mPid == INVALID_PID) {
		ret = findProcess(mName.c_str(), &mPid);
		if (ret < 0)
			return ret;

		snprintf(path, sizeof(path), "/proc/%d/stat", mPid);

		mStatFd = open(path, O_RDONLY|O_CLOEXEC);
		if (mStatFd == -1) {
			ret = -errno;
			printf("Fail to open %s : %d(%m)\n", path, errno);
			return ret;
		}

		ret = readStats(mStatFd, mPid, &mPrevStats);
		if (ret < 0) {
			printf("Can't find pid stats '%s'\n", mName.c_str());
			clear();
			return ret;
		}

		return -EAGAIN;
	}

	// Get stats
	ret = readStats(mStatFd, mPid, &rawStats);
	if (ret < 0) {
		printf("Can't find pid stats '%s'\n", mName.c_str());
		clear();
		return ret;
	}

	stats.mTs = ts;
	stats.mPid = mPid;
	stats.mName = mName.c_str();
	stats.mUtime = rawStats.utime;
	stats.mStime = rawStats.stime;

	stats.mCpuLoad = getCpuLoad(mPrevStats,
				    rawStats,
				    mSysSettings,
				    timeDiff);

	stats.mVsize = rawStats.vsize / 1024;
	stats.mRss = rawStats.rss * mSysSettings->mPagesize / 1024;
	stats.mThreadCount = rawStats.num_threads;

	// Disable fd count. Should be disabled by default and activable
	// by user.
	// stats.mFdCount = getPidFdCount();

	mPrevStats = rawStats;

	if (cb.mProcessStats)
		cb.mProcessStats(stats);

	// Process threads
	processThreads(ts, timeDiff, rawStats.num_threads, cb);

	return 0;
}

int ProcessMonitor::getPidFdCount()
{
	DIR *d;
	struct dirent entry;
	struct dirent *result = nullptr;
	char path[64];
	int ret;
	bool found = false;
	int count = 0;

	snprintf(path, sizeof(path), "/proc/%d/fd", mPid);

	d = opendir(path);
	if (!d) {
		ret = -errno;
		printf("Fail to open /proc : %d(%m)\n", errno);
		return ret;
	}

	while (!found) {
		ret = readdir_r(d, &entry, &result);
		if (ret == 0 && !result)
			break;

		count++;
	}

	closedir(d);

	return count;
}

int ProcessMonitor::readStats(int fd, int pid, RawStats *procstat)
{
	char strstat[1024];
	ssize_t readRet;
	int ret;

	readRet = pread(fd, strstat, sizeof(strstat), 0);
	if (readRet == -1) {
		ret = -errno;
		printf("read() failed : %d(%m)", errno);
		return ret;
	}

	// Remove trailing '\n'
	strstat[readRet - 1] = '\0';

	// Extract process name
	ret = sscanf(strstat, STAT_PATTERN,
		     &procstat->pid,
		     procstat->name,
		     &procstat->state,
		     &procstat->ppid,
		     &procstat->pgrp,
		     &procstat->session,
		     &procstat->tty_nr,
		     &procstat->tpgid,
		     &procstat->flags,
		     &procstat->minflt,
		     &procstat->cminflt,
		     &procstat->majflt,
		     &procstat->cmajflt,
		     &procstat->utime,
		     &procstat->stime,
		     &procstat->cutime,
		     &procstat->cstime,
		     &procstat->priority,
		     &procstat->nice,
		     &procstat->num_threads,
		     &procstat->itrealvalue,
		     &procstat->starttime,
		     &procstat->vsize,
		     &procstat->rss);
	if (ret != STAT_PATTERN_COUNT) {
		ret = -EINVAL;
		printf("Fail to parse stats for pid %d\n", pid);
		goto error;
	}

	return 0;

error:
	return ret;

}

bool ProcessMonitor::testPidName(int pid, const char *name)
{
	RawStats procstat;
	size_t procNameLen;
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

	ret = readStats(fd, pid, &procstat);
	close(fd);
	if (ret < 0) {
		return false;
	}

	procNameLen = strlen(procstat.name);
	if (procNameLen == 0) {
		printf("Got an empty process name for pid %d\n", pid);
		return false;
	}

	// Compare but exclude first and last caracter.
	// procfs give name like '(<processname>)'
	return (strncmp(name, procstat.name + 1, procNameLen - 2)) == 0;
}

int ProcessMonitor::findProcess(const char *name, int *outPid)
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

class SystemMonitorImpl : public SystemMonitor {
private:
	Callbacks mCb;
	SystemSettings mSysSettings;
	std::list<ProcessMonitor *> mMonitors;
	uint64_t mLastProcess;

public:
	SystemMonitorImpl(const Callbacks &cb);
	virtual ~SystemMonitorImpl();

	virtual int addProcess(const char *name);
	virtual int process();
};

SystemMonitorImpl::SystemMonitorImpl(const Callbacks &cb) : SystemMonitor()
{
	mCb = cb;
	mSysSettings.mClkTck = sysconf(_SC_CLK_TCK);
	mSysSettings.mPagesize = getpagesize();
	mLastProcess = 0;
}

SystemMonitorImpl::~SystemMonitorImpl()
{
	for (auto &m :mMonitors)
		delete m;
}

int SystemMonitorImpl::addProcess(const char *name)
{
	ProcessMonitor *monitor;

	if (!name)
		return -EINVAL;

	monitor = new ProcessMonitor(name, &mSysSettings);
	if (!monitor)
		return -ENOMEM;

	mMonitors.push_back(monitor);

	return 0;
}

static int getTimeUs(uint64_t *us)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		ret = -errno;
		printf("clock_gettime() failed : %d(%m)", errno);
		return ret;
	}

	*us = ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000UL;

	return 0;
}

int SystemMonitorImpl::process()
{
	uint64_t start = 0;
	uint64_t end = 0;
	int ret;

	// Compute delay between two calls
	ret = getTimeUs(&start);
	if (ret < 0)
		return ret;

	// Start process monitors
	for (auto &m :mMonitors)
		m->process(start, start - mLastProcess, mCb);

	// Compute acquisition duration
	ret = getTimeUs(&end);
	if (ret < 0)
		return ret;

	if (mCb.mAcquisitionDuration)
		mCb.mAcquisitionDuration( { start, end } );

	mLastProcess = start;

	return 0;
}

} // anonymous namespace

SystemMonitor *SystemMonitor::create(const Callbacks &cb)
{
	return new SystemMonitorImpl(cb);
}
