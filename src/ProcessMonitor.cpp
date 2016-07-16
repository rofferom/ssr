#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include <map>

#include "ProcFsTools.hpp"
#include "ProcessMonitor.hpp"

#define INVALID_PID -1

ProcessMonitor::ProcessMonitor(const char *name,
			       const SystemMonitor::SystemConfig *sysSettings)
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

int ProcessMonitor::addAndProcessThread(
		uint64_t ts,
		int tid,
		const SystemMonitor::Callbacks &cb)
{
	pfstools::RawStats rawStats;
	SystemMonitor::ThreadStats stats;
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

	ret = pfstools::readRawStats(info.mFd, &rawStats);
	if (ret < 0) {
		close(info.mFd);
		return ret;
	}

	ret = pfstools::readThreadStats(rawStats.mContent, &stats);
	if (ret < 0)
		return ret;

	snprintf(info.mName, sizeof(info.mName),
		 "%d-%s",
		 tid,
		 stats.mName);

	// Register thread
	auto insertRet = mThreads.insert( {tid, info} );
	if (!insertRet.second) {
		printf("Fail to insert thread %d\n", tid);
		close(info.mFd);
		return -EPERM;
	}

	// Notify thread stats
	stats.mTs = ts;
	strncpy(stats.mName, info.mName, sizeof(stats.mName));
	stats.mPid = mPid;

	if (cb.mThreadStats)
		cb.mThreadStats(stats, cb.mUserdata);

	return 0;
}

int ProcessMonitor::researchThreads(
		uint64_t ts,
		const SystemMonitor::Callbacks &cb)
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
			ret = addAndProcessThread(ts, tid, cb);
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

int ProcessMonitor::readRawStats()
{
	int ret;

	if (mPid == INVALID_PID) {
		char path[64];

		ret = pfstools::findProcess(mName.c_str(), &mPid);
		if (ret < 0)
			return ret;

		snprintf(path, sizeof(path), "/proc/%d/stat", mPid);

		mStatFd = open(path, O_RDONLY|O_CLOEXEC);
		if (mStatFd == -1) {
			ret = -errno;
			printf("Fail to open %s : %d(%m)\n", path, errno);
			return ret;
		}
	}

	// Read process stats
	ret = pfstools::readRawStats(mStatFd, &mRawStats);
	if (ret < 0) {
		close(mStatFd);
		mStatFd = -1;
		return ret;
	}

	for (auto &thread : mThreads) {
		ThreadInfo *threadInfo = &thread.second;
		pfstools::readRawStats(threadInfo->mFd, &threadInfo->mRawStats);
	}

	return 0;
}

int ProcessMonitor::processRawStats(uint64_t ts, const SystemMonitor::Callbacks &cb)
{
	int foundThreads;
	SystemMonitor::ThreadStats threadStats;
	SystemMonitor::ProcessStats processStats;
	int ret;

	if (!mRawStats.mPending)
		return 0;

	ret = pfstools::readProcessStats(mRawStats.mContent, &processStats);
	if (ret < 0)
		return ret;

	if (cb.mProcessStats) {
		processStats.mTs = mRawStats.mTs;
		cb.mProcessStats(processStats, cb.mUserdata);
	}

	// Process thread stats
	foundThreads = 0;
	for (auto &thread : mThreads) {
		ThreadInfo *threadInfo = &thread.second;

		if (!threadInfo->mRawStats.mPending)
			continue;

		ret = pfstools::readThreadStats(threadInfo->mRawStats.mContent,
						&threadStats);
		if (ret < 0)
			continue;

		if (cb.mThreadStats) {
			threadStats.mTs = threadInfo->mRawStats.mTs;

			strncpy(threadStats.mName, threadInfo->mName,
				sizeof(threadStats.mName));

			threadStats.mPid = mPid;

			cb.mThreadStats(threadStats, cb.mUserdata);
		}

		foundThreads++;
	}

	if (foundThreads != processStats.mThreadCount)
		researchThreads(ts, cb);

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
