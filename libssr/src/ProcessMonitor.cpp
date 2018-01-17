#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include "ssr_priv.hpp"

#define INVALID_PID -1

ProcessMonitor::ProcessMonitor(const char *name,
			       const SystemMonitor::Config *config,
			       const SystemMonitor::SystemConfig *sysSettings)
{
	mResearchType = ResearchType::byName;
	mState = AcqState::pending;
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
	mState = AcqState::pending;
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
	SystemMonitor::ThreadStats stats;
	ThreadInfo info;
	char path[128];
	int ret;

	// Open thread fd
	snprintf(path, sizeof(path),
		 "/proc/%d/task/%d/stat",
		 mPid, tid);

	ret = info.mRawStats.open(path);
	if (ret < 0)
		return ret;

	info.mTid = tid;

	// Read stats to get its name
	ret = pfstools::readRawStats(&info.mRawStats);
	if (ret < 0) {
		info.mRawStats.close();
		return ret;
	}

	ret = pfstools::readThreadStats(&info.mRawStats, &stats);
	if (ret < 0)
		return ret;

	snprintf(info.mName, sizeof(info.mName),
		 "%d-%s",
		 tid,
		 stats.mName);

	LOGD("Found new thread %s for process %d",
	     info.mName, mPid);

	// Register thread
	auto insertRet = mThreads.insert( {tid, info} );
	if (!insertRet.second) {
		LOGE("Fail to insert thread %d", tid);
		info.mRawStats.close();
		return -EPERM;
	}

	return 0;
}

int ProcessMonitor::findNewThreads()
{
	DIR *d;
	struct dirent *result;
	char path[128];
	int tid;
	char *endptr;
	int ret;

	snprintf(path, sizeof(path), "/proc/%d/task", mPid);

	d = opendir(path);
	if (!d) {
		ret = -errno;
		LOGE("Fail to open /proc : %d(%m)", errno);
		return ret;
	}

	ret = 0;
	while (true) {
		errno = 0;
		result = readdir(d);
		if (!result && !errno) {
			break;
		} else if (!result && errno) {
			ret = -errno;
			LOGE("readdir() failed : %d(%s)",
				 errno, strerror(errno));
			break;
		}

		tid = strtol(result->d_name, &endptr, 10);
		if (tid == LONG_MIN || tid == LONG_MAX || errno == EINVAL) {
			LOGE("Ignore %s", result->d_name);
			continue;
		} else if (*endptr != '\0') {
			continue;
		}

		// Avoid to add thread if already exists
		if (mThreads.count(tid) == 0) {
			ret = addNewThread(tid);
			if (ret < 0)
				LOGE("Fail to add thread %d", tid);
		}
	}

	closedir(d);

	return ret;
}

int ProcessMonitor::readRawThreadsStats()
{
	for (auto &thread : mThreads) {
		ThreadInfo *threadInfo = &thread.second;
		pfstools::readRawStats(&threadInfo->mRawStats);
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

		ret = pfstools::readThreadStats(&threadInfo->mRawStats,
						&threadStats);
		if (ret < 0)
			continue;

		if (cb.mThreadStats) {
			threadStats.mTs = threadInfo->mRawStats.mTs;
			threadStats.mAcqEnd = threadInfo->mRawStats.mAcqEnd;

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
		LOGE("Research type unknown : %d", (int) mResearchType);
		return -EINVAL;
	}

	snprintf(path, sizeof(path), "/proc/%d/stat", mPid);

	ret = mStats.open(path);
	if (ret < 0) {
		mState = AcqState::failed;
		return ret;
	}

	mState = AcqState::started;

	if (mResearchType == ResearchType::byName)
		LOGD("Found process '%s' : pid %d", mName.c_str(), mPid);
	else
		LOGD("Found process %d", mPid);

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

	// Close process fd. If the process hasn't been found, the fd is not
	// valid.
	mStats.close();

	// Close threads fd
	for (auto p : mThreads) {
		ThreadInfo &info = p.second;
		info.mRawStats.close();
	}

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

	if (mStats.mFd == -1) {
		mStats.mPending = false;
		return 0;
	}

	// Read process stats
	ret = pfstools::readRawStats(&mStats);
	if (ret < 0) {
		if (!mName.empty()) {
			LOGN("Process %d-%s has stopped",
			     mPid, mName.c_str());
		} else {
			LOGN("Process %d has stopped", mPid);
		}

		cleanProcessAndThreadsFd();
		mState = AcqState::failed;
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

	if (mState == AcqState::failed && mResearchType == ResearchType::byPid) {
		// Acquisition has failed at least once after a snapshot of
		// all the existing pid. It should mean that the process has
		// stopped. Avoid any further acquisition
		return 0;
	} else if (!mStats.mPending) {
		// Acquisition has failed. This means the process is currently not
		// known.
		ret = openProcessAndThreadsFd();
	} else {
		ret = pfstools::readProcessStats(&mStats, &processStats);
		if (ret < 0)
			return ret;

		if (mName.empty()) {
			mName = processStats.mName;
			LOGD("Process %d name found : %s",
			     mPid, processStats.mName);
		}

		if (cb.mProcessStats) {
			processStats.mTs = mStats.mTs;
			processStats.mAcqEnd = mStats.mAcqEnd;
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
