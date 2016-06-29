#ifndef __SYSTEM_MONITOR_HPP__
#define __SYSTEM_MONITOR_HPP__

#include <stdint.h>
#include <list>

#include <functional>

class SystemMonitor {
public:
	struct SystemStats {
		uint64_t mTs;
		uint32_t mCpuLoad;
		uint32_t mRamTotal;
		uint32_t mRamUsed;
	};

	struct ProcessStats {
		uint64_t    mTs;
		uint32_t    mPid;
		const char *mName;
		uint16_t    mCpuLoad;
		uint32_t    mVsize; // kbytes
		uint32_t    mRss; // kbytes
		uint16_t    mThreadCount;
		uint16_t    mFdCount;

		ProcessStats()
		{
			mTs = 0;
			mPid = 0;
			mName = nullptr;
			mCpuLoad = 0;
			mVsize = 0;
			mRss = 0;
			mThreadCount = 0;
			mFdCount = 0;
		}
	};

	struct ThreadStats {
		uint64_t    mTs;
		uint32_t    mPid;
		uint32_t    mTid;
		const char *mName;
		uint16_t    mCpuLoad;
	};

	struct Callbacks {
		std::function<void(const SystemStats &)> mSystemStats;
		std::function<void(const ProcessStats &)> mProcessStats;
		std::function<void(const ThreadStats &)> mThreadStats;
	};

public:
	virtual ~SystemMonitor() {}

	virtual int addProcess(const char *name) = 0;
	virtual int process() = 0;

	static SystemMonitor *create(const Callbacks &cb);
};

#endif // !__SYSTEM_MONITOR_HPP__