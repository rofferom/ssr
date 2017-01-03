#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "Log.hpp"
#include "SysStatsMonitor.hpp"

#define PROCSTAT_PATH "/proc/stat"

SysStatsMonitor::SysStatsMonitor()
{
	mFd = -1;
	mRawStats.mPending = false;
}

SysStatsMonitor::~SysStatsMonitor()
{
	if (mFd != -1)
		close(mFd);
}

int SysStatsMonitor::readRawStats()
{
	if (mFd == -1)
		return 0;

	return pfstools::readRawStats(mFd, &mRawStats);
}

int SysStatsMonitor::processRawStats(const SystemMonitor::Callbacks &cb)
{
	SystemMonitor::SystemStats stats;
	int ret;

	if (mFd == -1) {
		ret = open(PROCSTAT_PATH, O_RDONLY|O_CLOEXEC);
		if (ret == -1) {
			ret = -errno;
			LOGE("Fail to open %s : %d(%m)",
			     PROCSTAT_PATH, errno);
			return ret;
		}

		mFd = ret;

		return 0;
	} else if (!mRawStats.mPending) {
		return 0;
	}

	ret = pfstools::readSystemStats(mRawStats.mContent, &stats);
	if (ret < 0) {
		close(mFd);
		mFd = -1;
		return ret;
	}

	if (cb.mSystemStats) {
		stats.mTs = mRawStats.mTs;
		stats.mAcqEnd = mRawStats.mAcqEnd;
		cb.mSystemStats(stats, cb.mUserdata);
	}

	return 0;
}
