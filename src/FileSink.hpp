#ifndef __FILESINK_HPP__
#define __FILESINK_HPP__

#include <memory>

class ISink {
public:
	virtual ~ISink() {}

	virtual ssize_t write(const void *buff, size_t size) = 0;
};

class FileSink : public ISink {
private:
	FILE *mFile;

public:
	FileSink(FILE *file) : ISink()
	{
		mFile = file;
	}

	virtual ~FileSink()
	{
	}

	virtual ssize_t write(const void *buff, size_t size)
	{
		return fwrite(buff, size, 1, mFile);
	}
};

#endif // !__FILESINK_HPP__
