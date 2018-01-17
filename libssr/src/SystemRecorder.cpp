#include "ssr_priv.hpp"

#define FILE_FORMAT_VERSION 1

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

SystemRecorder::SystemRecorder()
{
	mFile = nullptr;
	mSink = nullptr;
}

SystemRecorder::~SystemRecorder()
{
	close();
}

int SystemRecorder::writeHeader()
{
	const std::list<StructDescRegistry::Type *> *typeList;
	int ret;

	ret = StructDescRegistry::getTypeList(&typeList);
	if (ret < 0) {
		LOGE("Fail to get type list : %d(%s)",
		     -ret, strerror(-ret));
		return ret;
	}

	// Format version
	ret = ValueTrait<uint8_t>::write(mSink, FILE_FORMAT_VERSION);
	RETURN_IF_WRITE_FAILED(ret);

	// Compressed
	ret = ValueTrait<uint8_t>::write(mSink, 0);
	RETURN_IF_WRITE_FAILED(ret);

	// StructDescList
	ret = ValueTrait<uint8_t>::write(mSink, typeList->size());
	RETURN_IF_WRITE_FAILED(ret);

	for (auto &i : *typeList) {
		ret = ValueTrait<uint8_t>::write(mSink, i->mId);
		RETURN_IF_WRITE_FAILED(ret);

		ret = ValueTrait<std::string>::write(mSink, i->mName);
		RETURN_IF_WRITE_FAILED(ret);

		ret = i->mDesc.writeDesc(mSink);
		RETURN_IF_WRITE_FAILED(ret);
	}

	return 0;
}

int SystemRecorder::open(const char *path)
{
	int ret;

	if (!path)
		return -EINVAL;
	else if (mFile)
		return -EPERM;

	mFile = fopen(path, "wb");
	if (!mFile) {
		ret = -errno;
		LOGE("Fail to open file '%s' : %d(%m)", path, errno);
		return ret;
	}

	mSink = new FileSink(mFile);
	if (!mSink) {
		ret = -ENOMEM;
		goto close_file;
	}

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

int SystemRecorder::close()
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

int SystemRecorder::flush()
{
	int ret;

	if (!mFile)
		return -EPERM;

	ret = fflush(mFile);
	if (ret < 0) {
		ret = -errno;
		LOG_ERRNO("fflush");
		return ret;
	}

	return 0;
}

static void registerSystemConfig()
{
	StructDesc *desc;
	const char *type;
	int ret;

	// SystemConfig
	type = "systemconfig";

	ret = StructDescRegistry::registerType<SystemConfig>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, SystemConfig, mClkTck, "clktck");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemConfig, mPagesize, "pagesize");
	RETURN_IF_REGISTER_FAILED(ret);
}

static void registerSystemStats()
{
	StructDesc *desc;
	const char *type;
	int ret;

	type = "systemstats";

	ret = StructDescRegistry::registerType<SystemStats>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mAcqEnd, "acqend");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mNice, "nice");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIdle, "idle");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIoWait, "iowait");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIrq, "irq");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mSoftIrq, "softirq");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mIrqCount, "irqcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mSoftIrqCount, "softirqcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mCtxSwitchCount, "ctxswitchcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mRamTotal, "ramtotal");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mRamAvailable, "ramavailable");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, SystemStats, mRamFree, "ramfree");
	RETURN_IF_REGISTER_FAILED(ret);
}

static void registerProcessStats()
{
	StructDesc *desc;
	const char *type;
	int ret;

	// ProcessStats
	type = "processstats";

	ret = StructDescRegistry::registerType<ProcessStats>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mAcqEnd, "acqend");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mPid, "pid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_STRING(desc, ProcessStats, mName, "name");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mVsize, "vsize");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mRss, "rss");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mThreadCount, "threadcount");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ProcessStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);
}

static void registerThreadStats()
{
	StructDesc *desc;
	const char *type;
	int ret;

	type = "threadstats";

	ret = StructDescRegistry::registerType<ThreadStats>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mTs, "ts");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mAcqEnd, "acqend");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mPid, "pid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mTid, "tid");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_STRING(desc, ThreadStats, mName, "name");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mUtime, "utime");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, ThreadStats, mStime, "stime");
	RETURN_IF_REGISTER_FAILED(ret);
}

static void registerAcqDuration()
{
	StructDesc *desc;
	const char *type;
	int ret;

	type = "acqduration";

	ret = StructDescRegistry::registerType<AcquisitionDuration>(type, &desc);
	RETURN_IF_REGISTER_TYPE_FAILED(ret, type);

	ret = REGISTER_RAW_VALUE(desc, AcquisitionDuration, mStart, "start");
	RETURN_IF_REGISTER_FAILED(ret);

	ret = REGISTER_RAW_VALUE(desc, AcquisitionDuration, mEnd, "end");
	RETURN_IF_REGISTER_FAILED(ret);
}

int SystemRecorder::registerStructDescs()
{
	registerSystemConfig();
	registerSystemStats();
	registerProcessStats();
	registerThreadStats();
	registerAcqDuration();

	return 0;
}
