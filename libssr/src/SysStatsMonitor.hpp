#ifndef __SYS_STATS_MONITOR__
#define __SYS_STATS_MONITOR__

class SysStatsMonitor {
private:
	int mFd;
	pfstools::RawStats mRawStats;

public:
	SysStatsMonitor();
	~SysStatsMonitor();

	int readRawStats();
	int processRawStats(const SystemMonitor::Callbacks &cb);

};

#endif // __SYS_STATS_MONITOR__