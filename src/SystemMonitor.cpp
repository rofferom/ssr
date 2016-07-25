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
	Config mConfig;
	Callbacks mCb;
	SystemConfig mSysSettings;

	SysStatsMonitor mSysMonitor;
	std::list<ProcessMonitor *> mProcMonitors;

private:
	int findAllProcesses();

public:
	SystemMonitorImpl(const Config &config, const Callbacks &cb);
	virtual ~SystemMonitorImpl();

	virtual int readSystemConfig(SystemConfig *config);
	virtual int addProcess(const char *name);
	virtual int loadProcesses();
	virtual int process();
};

SystemMonitorImpl::SystemMonitorImpl(
		const Config &config,
		const Callbacks &cb) : SystemMonitor()
{
	mConfig = config;
	mCb = cb;
	mSysSettings.mClkTck = sysconf(_SC_CLK_TCK);
	mSysSettings.mPagesize = getpagesize();
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

	monitor = new ProcessMonitor(name, &mConfig, &mSysSettings);
	if (!monitor)
		return -ENOMEM;

	mProcMonitors.push_back(monitor);

	return 0;
}

int SystemMonitorImpl::findAllProcesses()
{
	std::list<int> processList;
	ProcessMonitor *monitor;
	int ret;

	ret = pfstools::findAllProcesses(&processList);
	if (ret < 0) {
		printf("pfstools::findAllProcesses() failed : %d(%s\n)",
		       ret, strerror(-ret));

		return ret;
	}

	for (auto pid :processList) {
		monitor = new ProcessMonitor(pid, &mConfig, &mSysSettings);
		if (!monitor)
			return -ENOMEM;

		mProcMonitors.push_back(monitor);
	}

	return 0;
}

int SystemMonitorImpl::loadProcesses()
{
	if (mProcMonitors.empty()) {
		int ret;

		ret = findAllProcesses();
		if (ret < 0)
			return ret;
	}

	for (auto m :mProcMonitors)
		m->init();

	return 0;
}

int SystemMonitorImpl::process()
{
	AcquisitionDuration stats;
	int ret;

	// Compute delay between two calls
	ret = getTimeNs(&stats.mStart);
	if (ret < 0)
		return ret;

	mSysMonitor.readRawStats();

	// Start process monitors
	for (auto m :mProcMonitors)
		m->readRawStats();

	// Compute acquisition duration
	ret = getTimeNs(&stats.mEnd);
	if (ret < 0)
		return ret;

	if (mCb.mAcquisitionDuration)
		mCb.mAcquisitionDuration(stats, mCb.mUserdata);

	// Process fetched data
	mSysMonitor.processRawStats(mCb);

	for (auto &m :mProcMonitors)
		m->processRawStats(mCb);

	return 0;
}

} // anonymous namespace

SystemMonitor *SystemMonitor::create(const Config &config, const Callbacks &cb)
{
	return new SystemMonitorImpl(config, cb);
}
