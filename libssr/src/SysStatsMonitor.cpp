#include <fcntl.h>
#include <unistd.h>
#include "ssr_priv.hpp"

#define PROCSTAT_PATH "/proc/stat"
#define MEMINFO_PATH "/proc/meminfo"

SysStatsMonitor::SysStatsMonitor()
{
}

SysStatsMonitor::~SysStatsMonitor()
{
	mProcStats.close();
	mMemInfo.close();
}

int SysStatsMonitor::init()
{
	mProcStats.open(PROCSTAT_PATH);
	mMemInfo.open(MEMINFO_PATH);

	return 0;
}

int SysStatsMonitor::readRawStats()
{
	int ret;

	if (mProcStats.mFd == -1 || mMemInfo.mFd == -1)
		return 0;

	ret = pfstools::readRawStats(&mProcStats);
	if (ret < 0)
		return 0;

	ret = pfstools::readRawStats(&mMemInfo);
	if (ret < 0)
		return 0;

	return 0;
}

int SysStatsMonitor::checkStatFile(
		const char *path,
		pfstools::RawStats *rawStats)
{
	int ret;

	if (rawStats->mFd == -1)
		ret = rawStats->open(path);
	else if (!rawStats->mPending)
		ret = -EAGAIN;
	else
		ret = 0;

	return ret;
}

int SysStatsMonitor::processRawStats(const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::SystemStats stats;
	bool dataPending = false;
	int ret;

	// Check /proc/stat status
	ret = checkStatFile(PROCSTAT_PATH, &mProcStats);
	if (ret != -EAGAIN) {
		if (ret < 0) {
			LOGI("%s:%d", __FILE__, __LINE__);
			return ret;
		}

		ret = pfstools::readSystemStats(mProcStats.mContent, &stats);
		if (ret < 0) {
			mProcStats.close();
			return ret;
		}
	} else {
		dataPending = true;
	}

	// Check /proc/meminfo
	ret = checkStatFile(MEMINFO_PATH, &mMemInfo);
	if (ret != -EAGAIN) {
		if (ret < 0) {
			LOGI("%s:%d", __FILE__, __LINE__);
			return ret;
		}

		ret = pfstools::readMeminfoStats(mMemInfo.mContent, &stats);
		if (ret < 0) {
			mMemInfo.close();
			return ret;
		}
	} else {
		dataPending = true;
	}

	// Notify
	if (!dataPending && cb.mSystemStats) {
		stats.mTs = mProcStats.mTs;
		stats.mAcqEnd = mProcStats.mAcqEnd;
		cb.mSystemStats(stats, cb.mUserdata);
	}

	return 0;
}
