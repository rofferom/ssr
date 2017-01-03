#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include "StructDescRegistry.hpp"
#include "SystemRecorder.hpp"

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
