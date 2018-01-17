#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include "ssr_priv.hpp"

int SysStatsMonitor::DataSink::open()
{
	return mRawStats.open(mFsPath);
}

void SysStatsMonitor::DataSink::close()
{
	mRawStats.close();
}

bool SysStatsMonitor::DataSink::processRawStats(
		SystemMonitor::SystemStats *stats)
{
	int ret;

	// The sysfs file hasn't been opened yet: retry to open.
	// The first read will be done later, so we need to return false here.
	if (mRawStats.mFd == -1) {
		mRawStats.open(mFsPath);
		return false;
	}

	// There is no pending data
	if (!mRawStats.mPending)
		return false;

	assert(mParseStatsCb);
	ret = mParseStatsCb(mRawStats.mContent, stats);
	if (ret < 0) {
		mRawStats.close();
		return false;
	}

	return true;
}

SysStatsMonitor::SysStatsMonitor()
{
	mProcStats.mFsPath = "/proc/stat";
	mProcStats.mParseStatsCb = pfstools::readSystemStats;

	mMemInfo.mFsPath = "/proc/meminfo";
	mMemInfo.mParseStatsCb = pfstools::readMeminfoStats;
}

SysStatsMonitor::~SysStatsMonitor()
{
	mProcStats.close();
	mMemInfo.close();
}

int SysStatsMonitor::init()
{
	int ret;

	ret = mProcStats.open();
	if (ret < 0)
		return ret;

	ret = mMemInfo.open();
	if (ret < 0)
		return ret;

	return ret;
}

void SysStatsMonitor::readRawStats()
{
	pfstools::readRawStats(&mProcStats.mRawStats);
	pfstools::readRawStats(&mMemInfo.mRawStats);
}

int SysStatsMonitor::processRawStats(const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::SystemStats stats;
	bool newData = false;

	newData = mProcStats.processRawStats(&stats);
	newData |= mMemInfo.processRawStats(&stats);

	// Notify
	if (newData && cb.mSystemStats) {
		stats.mTs = mProcStats.mRawStats.mTs;
		stats.mAcqEnd = mMemInfo.mRawStats.mAcqEnd;
		cb.mSystemStats(stats, cb.mUserdata);
	}

	return 0;
}
