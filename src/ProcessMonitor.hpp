#ifndef __PROCESS_MONITOR_HPP__
#define __PROCESS_MONITOR_HPP__

#include "ProcFsTools.hpp"

class ProcessMonitor {
private:
	struct ThreadInfo {
		int mTid;
		int mFd;;
		char mName[64];

		pfstools::RawStats mRawStats;
	};

private:
	int mStatFd;
	int mPid;
	std::string mName;
	const SystemMonitor::SystemConfig *mSysSettings;

	pfstools::RawStats mRawStats;

	std::map<int, ThreadInfo> mThreads;

private:
	void clear();

	int getPidFdCount();

	int addAndProcessThread(uint64_t ts, int tid,
				const SystemMonitor::Callbacks &cb);

	int researchThreads(uint64_t ts,
			    const SystemMonitor::Callbacks &cb);

public:
	ProcessMonitor(const char *name,
		       const SystemMonitor::SystemConfig *sysSettings);

	~ProcessMonitor();

	int readRawStats();
	int processRawStats(uint64_t ts, const SystemMonitor::Callbacks &cb);

	const char *getName() const { return mName.c_str(); }
};

#endif // !__PROCESS_MONITOR_HPP__
