#ifndef __SYS_STATS_MONITOR__
#define __SYS_STATS_MONITOR__

class SysStatsMonitor {
private:
	struct DataSink {
		const char *mFsPath;
		pfstools::RawStats mRawStats;
		int (*mParseStatsCb)(char *s, SystemMonitor::SystemStats *stats);

		int open();
		void close();
		bool processRawStats(SystemMonitor::SystemStats *stats);
	};

private:
	// /proc/stat
	DataSink mProcStats;

	// /proc/meminfo
	DataSink mMemInfo;

public:
	SysStatsMonitor();
	~SysStatsMonitor();

	int init();

	void readRawStats();
	int processRawStats(const SystemMonitor::Callbacks &cb);
};

#endif // __SYS_STATS_MONITOR__
