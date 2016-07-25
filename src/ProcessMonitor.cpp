#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

#include <map>

#include "ProcFsTools.hpp"
#include "ProcessMonitor.hpp"

#define INVALID_PID -1

ProcessMonitor::ProcessMonitor(const char *name,
			       const SystemMonitor::Config *config,
			       const SystemMonitor::SystemConfig *sysSettings)
{
	mResearchType = ResearchType::byName;
	mStatFd = -1;
	mName = name;
	mPid = INVALID_PID;
	mConfig = config;
	mSysSettings = sysSettings;
}

ProcessMonitor::ProcessMonitor(int pid,
			       const SystemMonitor::Config *config,
			       const SystemMonitor::SystemConfig *sysSettings)
{
	mResearchType = ResearchType::byPid;
	mStatFd = -1;
	mPid = pid;
	mConfig = config;
	mSysSettings = sysSettings;
}


ProcessMonitor::~ProcessMonitor()
{
	cleanProcessAndThreadsFd();
}

int ProcessMonitor::addNewThread(int tid)
{
	pfstools::RawStats rawStats;
	SystemMonitor::ThreadStats stats;
	ThreadInfo info;
	char path[128];
	int ret;

	// Open thread fd
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

	// Read stats to get its name
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

	printf("Found new thread %s for process %d\n",
	       info.mName, mPid);

	// Register thread
	auto insertRet = mThreads.insert( {tid, info} );
	if (!insertRet.second) {
		printf("Fail to insert thread %d\n", tid);
		close(info.mFd);
		return -EPERM;
	}

	return 0;
}

int ProcessMonitor::findNewThreads()
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
			ret = addNewThread(tid);
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

int ProcessMonitor::readRawThreadsStats()
{
	for (auto &thread : mThreads) {
		ThreadInfo *threadInfo = &thread.second;

		pfstools::readRawStats(threadInfo->mFd,
				       &threadInfo->mRawStats);
	}

	return 0;
}

int ProcessMonitor::processRawThreadsStats(const SystemMonitor::Callbacks &cb)
{
	std::list<std::map<int, ThreadInfo>::iterator> removeList;
	SystemMonitor::ThreadStats threadStats;
	int ret;

	for (auto i = mThreads.begin(); i != mThreads.end(); i++) {
		ThreadInfo *threadInfo = &i->second;

		if (!threadInfo->mRawStats.mPending) {
			removeList.push_back(i);
			continue;
		}

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
	}

	// Clean thread that haven't been found
	for (auto &i :removeList)
		mThreads.erase(i);

	return 0;
}

int ProcessMonitor::openProcessAndThreadsFd()
{
	char path[64];
	int ret;

	// Open thread
	switch (mResearchType) {
	case ResearchType::byName:
		ret = pfstools::findProcess(mName.c_str(), &mPid);
		if (ret < 0)
			return ret;

		break;

	case ResearchType::byPid:
		// pid already known
		break;

	default:
		printf("Research type unknown : %d\n", (int) mResearchType);
		return -EINVAL;
	}

	snprintf(path, sizeof(path), "/proc/%d/stat", mPid);

	mStatFd = open(path, O_RDONLY|O_CLOEXEC);
	if (mStatFd == -1) {
		ret = -errno;
		printf("Fail to open %s : %d(%m)\n", path, errno);
		return ret;
	}

	if (mResearchType == ResearchType::byName)
		printf("Found process '%s' : pid %d\n", mName.c_str(), mPid);
	else
		printf("Found process %d\n", mPid);

	// Find threads
	if (mConfig->mRecordThreads) {
		ret = findNewThreads();
		if (ret < 0)
			return ret;
	}

	return 0;
}

int ProcessMonitor::cleanProcessAndThreadsFd()
{
	if (mResearchType == ResearchType::byName)
		mPid = INVALID_PID;

	// Close process fd
	close(mStatFd);
	mStatFd = -1;

	// Close threads fd
	for (auto p : mThreads)
		close(p.second.mFd);

	mThreads.clear();

	// Drop process if it doesn't exist anymore. Only when the research
	// is done by pid.
	if (mResearchType == ResearchType::byPid)
		mThreads.erase(mPid);

	return 0;
}

int ProcessMonitor::init()
{
	return openProcessAndThreadsFd();
}

int ProcessMonitor::readRawStats()
{
	int ret;

	if (mStatFd == -1) {
		mRawStats.mPending = false;
		return 0;
	}

	// Read process stats
	ret = pfstools::readRawStats(mStatFd, &mRawStats);
	if (ret < 0) {
		if (!mName.empty())
			printf("Process %s has stopped\n", mName.c_str());
		else
			printf("Process %d has stopped\n", mPid);

		cleanProcessAndThreadsFd();
		return ret;
	}

	// Process threads only if requested
	if (mConfig->mRecordThreads)
		readRawThreadsStats();

	return 0;
}

int ProcessMonitor::processRawStats(const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::ProcessStats processStats;
	int ret;

	// Acquisition has failed. This means the process is currently not
	// known.
	if (!mRawStats.mPending) {
		ret = openProcessAndThreadsFd();
	} else {
		ret = pfstools::readProcessStats(mRawStats.mContent,
						 &processStats);
		if (ret < 0)
			return ret;

		if (mName.empty()) {
			mName = processStats.mName;
			printf("Process %d name found : %s\n",
			       mPid, processStats.mName);
		}

		if (cb.mProcessStats) {
			processStats.mTs = mRawStats.mTs;
			cb.mProcessStats(processStats, cb.mUserdata);
		}

		// Process threads only if requested
		if (mConfig->mRecordThreads) {
			ret = processRawThreadsStats(cb);
			if (ret < 0)
				return ret;

			if (mThreads.size() != processStats.mThreadCount)
				findNewThreads();
		}
	}

	return ret;
}
