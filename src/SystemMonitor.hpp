#ifndef __SYSTEM_MONITOR_HPP__
#define __SYSTEM_MONITOR_HPP__

#include <stdint.h>
#include <list>

#include <functional>

class SystemMonitor {
public:
	struct SystemConfig {
		int32_t mClkTck;
	};

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
		uint32_t    mVsize; // kbytes
		uint32_t    mRss; // kbytes
		uint16_t    mThreadCount;
		uint16_t    mFdCount;

		uint64_t    mUtime;
		uint64_t    mStime;
	};

	struct ThreadStats {
		uint64_t    mTs;
		uint32_t    mPid;
		uint32_t    mTid;
		const char *mName;
		uint16_t    mCpuLoad;

		uint64_t    mUtime;
		uint64_t    mStime;
	};

	struct AcquisitionDuration {
		uint64_t    mStart;
		uint64_t    mEnd;
	};

	struct Callbacks {
		void (*mSystemStats) (const SystemStats &stats, void *userdata);
		void (*mProcessStats) (const ProcessStats &stats, void *userdata);
		void (*mThreadStats) (const ThreadStats &stats, void *userdata);
		void (*mAcquisitionDuration) (const AcquisitionDuration &stats, void *userdata);

		void *mUserdata;
	};

public:
	virtual ~SystemMonitor() {}

	virtual int readSystemConfig(SystemConfig *config) = 0;

	virtual int addProcess(const char *name) = 0;
	virtual int process() = 0;

	static SystemMonitor *create(const Callbacks &cb);
};

#endif // !__SYSTEM_MONITOR_HPP__
