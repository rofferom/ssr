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
	mRawProcStats.close();
	mRawMemInfo.close();
}

int SysStatsMonitor::init()
{
	mRawProcStats.open(PROCSTAT_PATH);
	mRawMemInfo.open(MEMINFO_PATH);

	return 0;
}

int SysStatsMonitor::readRawStats()
{
	int ret;

	if (mRawProcStats.mFd == -1 || mRawMemInfo.mFd == -1)
		return 0;

	ret = pfstools::readRawStats(&mRawProcStats);
	if (ret < 0)
		return 0;

	ret = pfstools::readRawStats(&mRawMemInfo);
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
	ret = checkStatFile(PROCSTAT_PATH, &mRawProcStats);
	if (ret != -EAGAIN) {
		if (ret < 0) {
			LOGI("%s:%d", __FILE__, __LINE__);
			return ret;
		}

		ret = pfstools::readSystemStats(mRawProcStats.mContent, &stats);
		if (ret < 0) {
			mRawProcStats.close();
			return ret;
		}
	} else {
		dataPending = true;
	}

	// Check /proc/meminfo
	ret = checkStatFile(MEMINFO_PATH, &mRawMemInfo);
	if (ret != -EAGAIN) {
		if (ret < 0) {
			LOGI("%s:%d", __FILE__, __LINE__);
			return ret;
		}

		ret = pfstools::readMeminfoStats(mRawMemInfo.mContent, &stats);
		if (ret < 0) {
			mRawMemInfo.close();
			return ret;
		}
	} else {
		dataPending = true;
	}

	// Notify
	if (!dataPending && cb.mSystemStats) {
		stats.mTs = mRawProcStats.mTs;
		stats.mAcqEnd = mRawProcStats.mAcqEnd;
		cb.mSystemStats(stats, cb.mUserdata);
	}

	return 0;
}
