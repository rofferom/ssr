#ifndef __FILESINK_HPP__
#define __FILESINK_HPP__

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <memory>

class ISink {
public:
	virtual ~ISink() {}

	virtual ssize_t write(const void *buff, size_t size) = 0;
	virtual ssize_t flush() = 0;
};

class FileSink : public ISink {
private:
	uint8_t mBuffer[4*1028];
	uint32_t mUsedSize;

	FILE *mFile;

public:
	FileSink(FILE *file) : ISink()
	{
		memset(mBuffer, 0, sizeof(mBuffer));
		mUsedSize = 0;
		mFile = file;
	}

	virtual ~FileSink()
	{
	}

	virtual ssize_t write(const void *buff, size_t size)
	{
		const uint8_t *src = (uint8_t *) buff;
		size_t remainingSize;
		size_t writeSize;
		ssize_t written = 0;

		while (size > 0) {
			remainingSize = sizeof(mBuffer) - mUsedSize;

			if (size > remainingSize)
				writeSize = remainingSize;
			else
				writeSize = size;

			memcpy(mBuffer + mUsedSize, src, writeSize);

			mUsedSize += writeSize;
			src  += writeSize;
			size -= writeSize;

			if (mUsedSize == sizeof(mBuffer)) {
				written += fwrite(mBuffer, 1, sizeof(mBuffer),
						  mFile);
				mUsedSize = 0;
			}
		}

		return written;
	}

	virtual ssize_t flush()
	{
		return fwrite(mBuffer, 1, mUsedSize, mFile);
	}
};

#endif // !__FILESINK_HPP__
