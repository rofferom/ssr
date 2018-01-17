#include <unistd.h>
#include "ssr_priv.hpp"

namespace {

int getTimeNs(uint64_t *ns)
{
	struct timespec ts;
	int ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (ret < 0) {
		ret = -errno;
		LOG_ERRNO("clock_gettime");
		return ret;
	}

	*ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;

	return 0;
}

class SystemMonitorImpl : public SystemMonitor {
private:
	enum class State {
		Stopped,
		Started,
	};

private:
	State mState;

	EventLoop *mLoop;
	Config mConfig;
	Callbacks mCb;
	SystemConfig mSysSettings;

	SysStatsMonitor mSysMonitor;
	std::list<ProcessMonitor *> mProcMonitors;

	Timer mPeriodTimer;

private:
	int findAllProcesses();
	int makeAcquisition();

public:
	SystemMonitorImpl(
			EventLoop *loop,
			const Config &config,
			const Callbacks &cb);
	virtual ~SystemMonitorImpl();

	int startAcquisitionTimer();

	virtual int readSystemConfig(SystemConfig *config);
	virtual int addProcess(const char *name);
	virtual int loadProcesses();
	virtual int clearProcesses();
	virtual int setAcqPeriod(int acqPeriod);
	virtual int start();
	virtual int stop();
};

SystemMonitorImpl::SystemMonitorImpl(
		EventLoop *loop,
		const Config &config,
		const Callbacks &cb) : SystemMonitor()
{
	mState = State::Stopped;
	mLoop = loop;
	mConfig = config;
	mCb = cb;
	mSysSettings.mClkTck = sysconf(_SC_CLK_TCK);
	mSysSettings.mPagesize = getpagesize();
}

SystemMonitorImpl::~SystemMonitorImpl()
{
	for (auto &m :mProcMonitors)
		delete m;

	mPeriodTimer.clear();
}

int SystemMonitorImpl::startAcquisitionTimer()
{
	struct timespec ts;
	int ret;

	auto cb = [this] () {
		LOGD("Start new acquisition");
		makeAcquisition();
	};

	ts.tv_sec = mConfig.mAcqPeriod;
	ts.tv_nsec = 0;

	ret = mPeriodTimer.setPeriodic(mLoop, ts, cb);
	if (ret < 0)
		return ret;

	return 0;
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
	if (ret < 0)
		return ret;

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

int SystemMonitorImpl::clearProcesses()
{
	for (auto m :mProcMonitors)
		delete m;

	mProcMonitors.clear();

	return 0;
}

int SystemMonitorImpl::setAcqPeriod(int acqPeriod)
{
	mConfig.mAcqPeriod = acqPeriod;

	if (mState == State::Started) {
		int ret;

		mPeriodTimer.clear();

		ret = startAcquisitionTimer();
		if (ret < 0) {
			LOGW("startAcquisitionTimer() failed : %d(%s)",
			     -ret, strerror(-ret));
			return ret;
		}
	}

	return 0;
}

int SystemMonitorImpl::start()
{
	int ret;

	ret = mSysMonitor.init();
	if (ret < 0) {
		LOGW("mSysMonitor.init() failed : %d(%s)",
		     -ret, strerror(-ret));
		return ret;
	}

	ret = startAcquisitionTimer();
	if (ret < 0) {
		LOGW("startAcquisitionTimer() failed : %d(%s)",
		     -ret, strerror(-ret));
		return ret;
	}

	ret = makeAcquisition();
	if (ret < 0) {
		LOGW("makeAcquisition() failed : %d(%s)",
		     -ret, strerror(-ret));
		return ret;

	}

	mState = State::Started;

	return 0;
}

int SystemMonitorImpl::stop()
{
	int ret;

	ret = mPeriodTimer.clear();
	if (ret < 0)
		return ret;

	mState = State::Stopped;

	return 0;
}

int SystemMonitorImpl::makeAcquisition()
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

	if (mCb.mResultsBegin)
		mCb.mResultsBegin(stats, mCb.mUserdata);

	// Process fetched data
	mSysMonitor.processRawStats(mCb);

	for (auto &m :mProcMonitors)
		m->processRawStats(mCb);

	if (mCb.mResultsEnd)
		mCb.mResultsEnd(mCb.mUserdata);

	return 0;
}

} // anonymous namespace

int SystemMonitor::create(
		EventLoop *loop,
		const Config &config,
		const Callbacks &cb,
		SystemMonitor **outMonitor)
{
	SystemMonitorImpl *monitor;

	if (!loop || !outMonitor)
		return -EINVAL;

	monitor = new SystemMonitorImpl(loop, config, cb);
	if (!monitor)
		return -ENOMEM;

	*outMonitor = monitor;

	return 0;
}
