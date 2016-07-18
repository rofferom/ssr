#ifndef __PROCESS_MONITOR_HPP__
#define __PROCESS_MONITOR_HPP__

#include <string>
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
	const SystemMonitor::Config *mConfig;
	const SystemMonitor::SystemConfig *mSysSettings;

	pfstools::RawStats mRawStats;

	std::map<int, ThreadInfo> mThreads;

private:
	int openProcessAndThreadsFd();

	int cleanProcessAndThreadsFd();

	int addNewThread(int tid);

	int findNewThreads();

	int readRawThreadsStats();

	int processRawThreadsStats(const SystemMonitor::Callbacks &cb);

public:
	ProcessMonitor(const char *name,
		       const SystemMonitor::Config *config,
		       const SystemMonitor::SystemConfig *sysSettings);

	~ProcessMonitor();

	int init();
	int readRawStats();
	int processRawStats(const SystemMonitor::Callbacks &cb);

	const char *getName() const { return mName.c_str(); }
};

#endif // !__PROCESS_MONITOR_HPP__
