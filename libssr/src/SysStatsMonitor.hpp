#ifndef __SYS_STATS_MONITOR__
#define __SYS_STATS_MONITOR__

class SysStatsMonitor {
private:
	// /proc/stat
	pfstools::RawStats mRawProcStats;

	// /proc/meminfo
	pfstools::RawStats mRawMemInfo;

private:
	static int checkStatFile(
		const char *path,
		pfstools::RawStats *rawStats);

public:
	SysStatsMonitor();
	~SysStatsMonitor();

	int init();

	int readRawStats();
	int processRawStats(const SystemMonitor::Callbacks &cb);

};

#endif // __SYS_STATS_MONITOR__
