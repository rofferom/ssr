#ifndef __SYS_STATS_MONITOR__
#define __SYS_STATS_MONITOR__

class SysStatsMonitor {
private:
	// /proc/stat
	int mProcStatFd;
	pfstools::RawStats mRawProcStats;

	// /proc/meminfo
	int mMeminfoFd;
	pfstools::RawStats mRawMemInfo;

private:
	static int checkStatFile(
		int *fd,
		const char *path,
		pfstools::RawStats *rawStats);
public:
	SysStatsMonitor();
	~SysStatsMonitor();

	int readRawStats();
	int processRawStats(const SystemMonitor::Callbacks &cb);

};

#endif // __SYS_STATS_MONITOR__
