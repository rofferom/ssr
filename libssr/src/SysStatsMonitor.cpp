#include <fcntl.h>
#include <unistd.h>
#include "ssr_priv.hpp"

#define PROCSTAT_PATH "/proc/stat"
#define MEMINFO_PATH "/proc/meminfo"

SysStatsMonitor::SysStatsMonitor()
{
	mProcStatFd = -1;
	mRawProcStats.mPending = false;

	mMeminfoFd = -1;
	mRawMemInfo.mPending = false;
}

SysStatsMonitor::~SysStatsMonitor()
{
	if (mProcStatFd != -1)
		close(mProcStatFd);

	if (mMeminfoFd != -1)
		close(mMeminfoFd);
}

int SysStatsMonitor::init()
{
	openFile(PROCSTAT_PATH, &mProcStatFd);
	openFile(MEMINFO_PATH, &mMeminfoFd);

	return 0;
}

int SysStatsMonitor::openFile(const char *path, int *fd)
{
	int ret;

	ret = open(path, O_RDONLY|O_CLOEXEC);
	if (ret == -1) {
		ret = -errno;
		LOGE("Fail to open %s : %d(%m)", path, errno);
		return ret;
	}

	*fd = ret;

	return 0;
}

int SysStatsMonitor::readRawStats()
{
	int ret;

	if (mProcStatFd == -1 || mMeminfoFd == -1)
		return 0;

	ret = pfstools::readRawStats(mProcStatFd, &mRawProcStats);
	if (ret < 0)
		return 0;

	ret = pfstools::readRawStats(mMeminfoFd, &mRawMemInfo);
	if (ret < 0)
		return 0;

	return 0;
}

int SysStatsMonitor::checkStatFile(
		int *fd,
		const char *path,
		pfstools::RawStats *rawStats)
{
	int ret;

	if (*fd == -1)
		ret = openFile(path, fd);
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
	ret = checkStatFile(&mProcStatFd, PROCSTAT_PATH, &mRawProcStats);
	if (ret != -EAGAIN) {
		if (ret < 0) {
			LOGI("%s:%d", __FILE__, __LINE__);
			return ret;
		}

		ret = pfstools::readSystemStats(mRawProcStats.mContent, &stats);
		if (ret < 0) {
			close(mProcStatFd);
			mProcStatFd = -1;
			return ret;
		}
	} else {
		dataPending = true;
	}

	// Check /proc/meminfo
	ret = checkStatFile(&mMeminfoFd, MEMINFO_PATH, &mRawMemInfo);
	if (ret != -EAGAIN) {
		if (ret < 0) {
			LOGI("%s:%d", __FILE__, __LINE__);
			return ret;
		}

		ret = pfstools::readMeminfoStats(mRawMemInfo.mContent, &stats);
		if (ret < 0) {
			close(mMeminfoFd);
			mMeminfoFd = -1;
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
