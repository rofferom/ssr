#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include "FileSink.hpp"
#include "StructDesc.hpp"
#include "SystemRecorder.hpp"

#define FILE_FORMAT_VERSION 1

#define RETURN_IF_WRITE_FAILED(ret) \
	if (ret < 0) { \
		printf("%s:%d: write() failed : %d(%s)\n", \
		       __FILE__, __LINE__, -ret, strerror(-ret)); \
	}

#define RETURN_IF_REGISTER_FAILED(ret) \
	if (ret < 0) { \
		printf("%s:%d: register() failed : %d(%s)\n", \
		       __FILE__, __LINE__, -ret, strerror(-ret)); \
	}

namespace {

/**
 * FileHeader
 * 	version: u8
 * 	compressed: u8
 * RecordDescList
 *
 * Record
 * 	type
 * 	payload
 */

enum RecordType : uint8_t {
	RECORD_TYPE_SYS_CONFIG = 0,
	RECORD_TYPE_SYS_STATS,
	RECORD_TYPE_PROCESS_STATS,
	RECORD_TYPE_THREAD_STATS,
	RECORD_TYPE_ACQ_DURATION,

	RECORD_TYPE_COUNT
};

class SystemRecorderImpl : public SystemRecorder {
private:
	struct StructDescInternal {
		std::string mName;
		StructDesc mDesc;
	};

private:
	FILE *mFile;
	FileSink *mSink;

	StructDescInternal *mDescList[RECORD_TYPE_COUNT];

private:
	int initDescs();

	int writeHeader();

public:
	SystemRecorderImpl();
	virtual ~SystemRecorderImpl();

	virtual int open(const char *path);
	virtual int close();

	virtual int flush();

	virtual int record(const SystemMonitor::SystemConfig &config);
	virtual int record(const SystemMonitor::SystemStats &stats);
	virtual int record(const SystemMonitor::ProcessStats &stats);
	virtual int record(const SystemMonitor::ThreadStats &stats);
	virtual int record(const SystemMonitor::AcquisitionDuration &duration);
};

SystemRecorderImpl::SystemRecorderImpl() : SystemRecorder()
{
	mFile = nullptr;
	mSink = nullptr;

	for (uint32_t i = 0; i < RECORD_TYPE_COUNT; i++)
		mDescList[i] = nullptr;
}

SystemRecorderImpl::~SystemRecorderImpl()
{
	close();

	for (uint32_t i = 0; i < RECORD_TYPE_COUNT; i++)
		delete mDescList[i];
}

int SystemRecorderImpl::initDescs()
{
	StructDescInternal *desc;
	int ret;

	// SystemConfig
	desc = new StructDescInternal();
	if (!desc)
		return -ENOMEM;

	desc->mName = "systemconfig";

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::SystemConfig, mClkTck, "clktck");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::SystemConfig, mPagesize, "pagesize");
	RETURN_IF_REGISTER_FAILED(ret);


	mDescList[RECORD_TYPE_SYS_CONFIG] = desc;
	desc = nullptr;


	// SystemStats
	desc = new StructDescInternal();
	if (!desc)
		return -ENOMEM;

	desc->mName = "systemstats";

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::SystemStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::SystemStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::SystemStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::SystemStats, mIdleTime, "idletime");
	RETURN_IF_REGISTER_FAILED(ret);

	mDescList[RECORD_TYPE_SYS_STATS] = desc;
	desc = nullptr;

	// ProcessStats
	desc = new StructDescInternal();
	if (!desc)
		return -ENOMEM;

	desc->mName = "processstats";

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mPid, "pid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_STRING(&desc->mDesc, SystemMonitor::ProcessStats, mName, "name");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mVsize, "vsize");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mRss, "rss");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mThreadCount, "threadcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mFdCount, "fdcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ProcessStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	mDescList[RECORD_TYPE_PROCESS_STATS] = desc;
	desc = nullptr;

	// ThreadStats
	desc = new StructDescInternal();
	if (!desc)
		return -ENOMEM;

	desc->mName = "threadstats";

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ThreadStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ThreadStats, mPid, "pid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ThreadStats, mTid, "tid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_STRING(&desc->mDesc, SystemMonitor::ThreadStats, mName, "name");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ThreadStats, mCpuLoad, "cpuload");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ThreadStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::ThreadStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	mDescList[RECORD_TYPE_THREAD_STATS] = desc;
	desc = nullptr;

	// Acquisition duration
	desc = new StructDescInternal();
	if (!desc)
		return -ENOMEM;

	desc->mName = "acqduration";

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::AcquisitionDuration, mStart, "start");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(&desc->mDesc, SystemMonitor::AcquisitionDuration, mEnd, "end");
	RETURN_IF_REGISTER_FAILED(ret);

	mDescList[RECORD_TYPE_ACQ_DURATION] = desc;
	desc = nullptr;

	return 0;
}

int SystemRecorderImpl::writeHeader()
{
	int ret;

	// Format version
	ret = ValueTrait<uint8_t>::write(mSink, FILE_FORMAT_VERSION);
	RETURN_IF_WRITE_FAILED(ret);

	// Compressed
	ret = ValueTrait<uint8_t>::write(mSink, 0);
	RETURN_IF_WRITE_FAILED(ret);

	// StructDescList
	ret = ValueTrait<uint8_t>::write(mSink, RECORD_TYPE_COUNT);
	RETURN_IF_WRITE_FAILED(ret);

	for (uint32_t i = 0; i < RECORD_TYPE_COUNT; i++) {
		if (!mDescList[i]) {
			printf("No desc for record %d\n", i);
			return -EINVAL;
		}

		ret = ValueTrait<uint8_t>::write(mSink, i);
		RETURN_IF_WRITE_FAILED(ret);

		ret = ValueTrait<std::string>::write(mSink, mDescList[i]->mName);
		RETURN_IF_WRITE_FAILED(ret);

		ret = mDescList[i]->mDesc.writeDesc(mSink);
		RETURN_IF_WRITE_FAILED(ret);
	}

	return 0;
}

int SystemRecorderImpl::open(const char *path)
{
	int ret;

	if (!path)
		return -EINVAL;
	else if (mFile)
		return -EPERM;

	mFile = fopen(path, "wb");
	if (!mFile) {
		ret = -errno;
		printf("Fail to open file '%s' : %d(%m)\n", path, errno);
		return ret;
	}

	mSink = new FileSink(mFile);
	if (!mSink) {
		ret = -ENOMEM;
		goto close_file;
	}

	ret = initDescs();
	if (ret < 0)
		goto clear_sink;

	ret = writeHeader();
	if (ret < 0)
		goto clear_sink;

	return 0;

clear_sink:
	delete mSink;
	mSink = nullptr;
close_file:
	fclose(mFile);
	mFile = nullptr;

	return ret;
}

int SystemRecorderImpl::close()
{
	if (!mFile)
		return -EPERM;

	mSink->flush();
	delete mSink;
	mSink = nullptr;

	fclose(mFile);
	mFile = nullptr;

	return 0;
}

int SystemRecorderImpl::flush()
{
	int ret;

	if (!mFile)
		return -EPERM;

	ret = fflush(mFile);
	if (ret < 0) {
		ret = -errno;
		printf("fflush() failed : %d(%m)\n", errno);
		return ret;
	}

	return 0;
}

int SystemRecorderImpl::record(const SystemMonitor::SystemConfig &config)
{
	int ret;

	ret = ValueTrait<uint8_t>::write(mSink, RECORD_TYPE_SYS_CONFIG);
	RETURN_IF_WRITE_FAILED(ret);

	ret = mDescList[RECORD_TYPE_SYS_CONFIG]->mDesc.writeValue(mSink, &config);
	RETURN_IF_WRITE_FAILED(ret);

	return 0;
}

int SystemRecorderImpl::record(const SystemMonitor::SystemStats &stats)
{
	int ret;

	ret = ValueTrait<uint8_t>::write(mSink, RECORD_TYPE_SYS_STATS);
	RETURN_IF_WRITE_FAILED(ret);

	ret = mDescList[RECORD_TYPE_SYS_STATS]->mDesc.writeValue(mSink, &stats);
	RETURN_IF_WRITE_FAILED(ret);

	return 0;
}

int SystemRecorderImpl::record(const SystemMonitor::ProcessStats &stats)
{
	int ret;

	ret = ValueTrait<uint8_t>::write(mSink, RECORD_TYPE_PROCESS_STATS);
	RETURN_IF_WRITE_FAILED(ret);

	ret = mDescList[RECORD_TYPE_PROCESS_STATS]->mDesc.writeValue(mSink, &stats);
	RETURN_IF_WRITE_FAILED(ret);

	return 0;
}

int SystemRecorderImpl::record(const SystemMonitor::ThreadStats &stats)
{
	int ret;

	ret = ValueTrait<uint8_t>::write(mSink, RECORD_TYPE_THREAD_STATS);
	RETURN_IF_WRITE_FAILED(ret);

	ret = mDescList[RECORD_TYPE_THREAD_STATS]->mDesc.writeValue(mSink, &stats);
	RETURN_IF_WRITE_FAILED(ret);

	return 0;
}

int SystemRecorderImpl::record(const SystemMonitor::AcquisitionDuration &duration)
{
	int ret;

	ret = ValueTrait<uint8_t>::write(mSink, RECORD_TYPE_ACQ_DURATION);
	RETURN_IF_WRITE_FAILED(ret);

	ret = mDescList[RECORD_TYPE_ACQ_DURATION]->mDesc.writeValue(mSink, &duration);
	RETURN_IF_WRITE_FAILED(ret);

	return 0;
}

} // anonymous namespace

SystemRecorder *SystemRecorder::create()
{
	return new SystemRecorderImpl();
}
