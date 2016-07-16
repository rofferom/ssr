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

#include "ProcessMonitor.hpp"
#include "SysStatsMonitor.hpp"
#include "SystemMonitor.hpp"
#include "ProcFsTools.hpp"

namespace {

int getTimeNs(uint64_t *ns)
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

	SysStatsMonitor mSysMonitor;
	std::list<ProcessMonitor *> mProcMonitors;
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
	for (auto &m :mProcMonitors)
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

	mProcMonitors.push_back(monitor);

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

	mSysMonitor.readRawStats();

	// Start process monitors
	for (auto m :mProcMonitors)
		m->readRawStats();

	// Compute acquisition duration
	ret = getTimeNs(&end);
	if (ret < 0)
		return ret;

	if (mCb.mAcquisitionDuration)
		mCb.mAcquisitionDuration( { start, end }, mCb.mUserdata);

	// Process fetched data
	mSysMonitor.processRawStats(start, mCb);

	for (auto &m :mProcMonitors)
		m->processRawStats(start, mCb);

	mLastProcess = start;

	return 0;
}

} // anonymous namespace

SystemMonitor *SystemMonitor::create(const Callbacks &cb)
{
	return new SystemMonitorImpl(cb);
}
