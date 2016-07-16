#ifndef __SYS_STATS_MONITOR__
#define __SYS_STATS_MONITOR__

#include "ProcFsTools.hpp"

class SysStatsMonitor {
private:
	int mFd;
	pfstools::RawStats mRawStats;

public:
	SysStatsMonitor();
	~SysStatsMonitor();

	int readRawStats();
	int processRawStats(uint64_t ts, const SystemMonitor::Callbacks &cb);

};

#endif // __SYS_STATS_MONITOR__
