#ifndef __SYSTEM_MONITOR_HPP__
#define __SYSTEM_MONITOR_HPP__

class EventLoop;

class SystemMonitor {
public:
	struct SystemConfig {
		int32_t mClkTck;
		int32_t mPagesize ;
	};

	struct SystemStats {
		uint64_t    mTs;
		uint64_t    mAcqEnd;

		// Durations
		uint64_t    mUtime;
		uint64_t    mNice;
		uint64_t    mStime;
		uint64_t    mIdle;
		uint64_t    mIoWait;
		uint64_t    mIrq;
		uint64_t    mSoftIrq;

		// Occurences
		uint64_t    mIrqCount;
		uint64_t    mSoftIrqCount;
		uint64_t    mCtxSwitchCount;

		// Ram usage
		uint64_t    mRamTotal;
		uint64_t    mRamAvailable;
		uint64_t    mRamFree;
	};

	struct ProcessStats {
		uint64_t    mTs;
		uint64_t    mAcqEnd;

		uint32_t    mPid;
		char        mName[64];
		uint32_t    mVsize;
		uint32_t    mRss;
		uint16_t    mThreadCount;

		uint64_t    mUtime;
		uint64_t    mStime;
	};

	struct ThreadStats {
		uint64_t    mTs;
		uint64_t    mAcqEnd;

		uint32_t    mPid;
		uint32_t    mTid;
		char        mName[64];

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

		Callbacks()
		{
			mSystemStats = nullptr;
			mProcessStats = nullptr;
			mThreadStats = nullptr;
			mAcquisitionDuration = nullptr;
			mUserdata = nullptr;
		}
	};

	struct Config {
		bool mRecordThreads;
		int mAcqPeriod; // seconds

		Config()
		{
			mRecordThreads = true;
			mAcqPeriod = 1;
		}
	};

public:
	virtual ~SystemMonitor() {}

	virtual int readSystemConfig(SystemConfig *config) = 0;

	virtual int addProcess(const char *name) = 0;

	virtual int loadProcesses() = 0;

	static int create(
			EventLoop *loop,
			const Config &config,
			const Callbacks &cb,
			SystemMonitor **monitor);

	static int initStructDescs();
};

#endif // !__SYSTEM_MONITOR_HPP__
