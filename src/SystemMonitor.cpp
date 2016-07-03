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
#include "ProcFsTools.hpp"

namespace {

#define INVALID_PID -1

class ProcessMonitor {
private:
	struct ThreadInfo {
		int mTid;
		int mFd;;
		char mName[64];
	};

private:
	int mStatFd;
	int mPid;
	std::string mName;
	const SystemMonitor::SystemConfig *mSysSettings;

	std::map<int, ThreadInfo> mThreads;

private:
	void clear();

	int getPidFdCount();

	int addAndProcessThread(uint64_t ts, int tid,
				const SystemMonitor::Callbacks &cb);

	int researchThreads(uint64_t ts,
			    const SystemMonitor::Callbacks &cb);

	int processThreads(uint64_t ts,
			   long int numThreads,
			   const SystemMonitor::Callbacks &cb);

	int readThreadStats(ThreadInfo *info,
			    uint64_t ts,
			    const SystemMonitor::Callbacks &cb);

	int readProcessStats(uint64_t ts,
					     SystemMonitor::ProcessStats *stats,
					     const SystemMonitor::Callbacks &cb);

public:
	ProcessMonitor(const char *name, const SystemMonitor::SystemConfig *sysSettings);
	~ProcessMonitor();

	int process(uint64_t ts, const SystemMonitor::Callbacks &cb);
};

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

	ret = pfstools::readThreadStats(info.mFd, &stats);
	if (ret < 0) {
		close(info.mFd);
		return ret;
	}

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

int ProcessMonitor::readThreadStats(ThreadInfo *info,
				    uint64_t ts,
				    const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::ThreadStats stats;
	int ret;

	ret = pfstools::readThreadStats(info->mFd, &stats);
	if (ret < 0) {
		// Thread finished
		printf("Can't read thread stats '%d'\n", info->mTid);
		return ret;
	}

	stats.mTs = ts;
	strncpy(stats.mName, info->mName, sizeof(stats.mName));
	stats.mPid = mPid;

	if (cb.mThreadStats)
		cb.mThreadStats(stats, cb.mUserdata);

	return 0;
}

int ProcessMonitor::readProcessStats(uint64_t ts,
				     SystemMonitor::ProcessStats *stats,
				     const SystemMonitor::Callbacks &cb)
{
	int ret;

	// Get stats
	ret = pfstools::readProcessStats(mStatFd, stats);
	if (ret < 0) {
		printf("Can't find pid stats '%s'\n", mName.c_str());
		return ret;
	}

	stats->mTs = ts;

	// Disable fd count. Should be disabled by default and activable
	// by user.
	// stats.mFdCount = getPidFdCount();

	if (cb.mProcessStats)
		cb.mProcessStats(*stats, cb.mUserdata);

	return 0;
}

int ProcessMonitor::processThreads(uint64_t ts,
				   long int numThreads,
				   const SystemMonitor::Callbacks &cb)
{
	long int foundThreads;
	int ret;

	// Process all already known threads.
	foundThreads = 0;
	for (auto it = mThreads.begin(); it != mThreads.end(); it++) {
		ThreadInfo *info = &it->second;

		ret = readThreadStats(info, ts, cb);
		if (ret < 0) {
			printf("Fail to process thread %d\n", info->mTid);
			close(info->mFd);
			mThreads.erase(it);
		} else {
			foundThreads++;
		}
	}

	/**
	 * If there are missing threads, research any new thread.
	 * This can happen if :
	 * - a new thread has been created
	 * - a known thread has stopped
	 */
	if (foundThreads != numThreads)
		researchThreads(ts, cb);

	return 0;
}

int ProcessMonitor::process(uint64_t ts, const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::ProcessStats stats;
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

	// Get stats
	ret = readProcessStats(ts, &stats, cb);
	if (ret < 0) {
		clear();
		return ret;
	}

	// Process threads
	processThreads(ts, stats.mThreadCount, cb);

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

class SystemMonitorImpl : public SystemMonitor {
private:
	Callbacks mCb;
	SystemConfig mSysSettings;
	std::list<ProcessMonitor *> mMonitors;
	uint64_t mLastProcess;

public:
	SystemMonitorImpl(const Callbacks &cb);
	virtual ~SystemMonitorImpl();

	virtual int readSystemConfig(SystemConfig *config);
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

int SystemMonitorImpl::readSystemConfig(SystemConfig *config)
{
	if (!config)
		return -EINVAL;

	*config = mSysSettings;

	return 0;
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

int SystemMonitorImpl::process()
{
	uint64_t start = 0;
	uint64_t end = 0;
	int ret;

	// Compute delay between two calls
	ret = getTimeNs(&start);
	if (ret < 0)
		return ret;

	// Start process monitors
	for (auto &m :mMonitors)
		m->process(start, mCb);

	// Compute acquisition duration
	ret = getTimeNs(&end);
	if (ret < 0)
		return ret;

	if (mCb.mAcquisitionDuration)
		mCb.mAcquisitionDuration( { start, end }, mCb.mUserdata);

	mLastProcess = start;

	return 0;
}

} // anonymous namespace

SystemMonitor *SystemMonitor::create(const Callbacks &cb)
{
	return new SystemMonitorImpl(cb);
}
