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

int SystemMonitor::initStructDescs()
{
	StructDesc *desc;
	const char *type;
	int ret;

	// SystemConfig
	type = "systemconfig";

	ret = StructDescRegistry::registerType<SystemConfig>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, SystemConfig, mClkTck, "clktck");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemConfig, mPagesize, "pagesize");
	RETURN_IF_REGISTER_FAILED(ret);

	// SystemStats
	type = "systemstats";

	ret = StructDescRegistry::registerType<SystemStats>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mAcqEnd, "acqend");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mNice, "nice");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIdle, "idle");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIoWait, "iowait");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIrq, "irq");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mSoftIrq, "softirq");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIrqCount, "irqcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mSoftIrqCount, "softirqcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mCtxSwitchCount, "ctxswitchcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mRamTotal, "ramtotal");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mRamAvailable, "ramavailable");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mRamFree, "ramfree");
	RETURN_IF_REGISTER_FAILED(ret);

	// ProcessStats
	type = "processstats";

	ret = StructDescRegistry::registerType<ProcessStats>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mAcqEnd, "acqend");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mPid, "pid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_STRING(desc, ProcessStats, mName, "name");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mVsize, "vsize");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mRss, "rss");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mThreadCount, "threadcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mPriority, "priority");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mNice, "nice");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mRtPriority, "rtpriority");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mPolicy, "policy");
	RETURN_IF_REGISTER_FAILED(ret);

	// ThreadStats
	type = "threadstats";

	ret = StructDescRegistry::registerType<ThreadStats>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mAcqEnd, "acqend");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mPid, "pid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mTid, "tid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_STRING(desc, ThreadStats, mName, "name");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mPriority, "priority");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mNice, "nice");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mRtPriority, "rtpriority");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mPolicy, "policy");
	RETURN_IF_REGISTER_FAILED(ret);

	// Acquisition duration
	type = "acqduration";

	ret = StructDescRegistry::registerType<AcquisitionDuration>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, AcquisitionDuration, mStart, "start");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, AcquisitionDuration, mEnd, "end");
	RETURN_IF_REGISTER_FAILED(ret);

	return 0;
}
