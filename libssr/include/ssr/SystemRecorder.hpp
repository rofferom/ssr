#ifndef __SYSTEM_RECORDER_HPP__
#define __SYSTEM_RECORDER_HPP__

struct ProgramParameters {
	char mParams[1024];
};

class SystemRecorder {
private:
	FILE *mFile;
	FileSink *mSink;

private:
	int writeHeader();

public:
	SystemRecorder();
	virtual ~SystemRecorder();

	virtual int open(const char *path);
	virtual int close();

	virtual int flush();

	template <typename T>
	int record(const T &params)
	{
		const StructDescRegistry::Type *type;
		int ret;

		ret = StructDescRegistry::getType<T>(&type);
		if (ret < 0) {
			LOGE("Fail to get type %s", typeid(T).name());
			return ret;
		}

		ret = ValueTrait<uint8_t>::write(mSink, type->mId);
		RETURN_IF_WRITE_FAILED(ret);

		ret = type->mDesc.writeValue(mSink, &params);
		RETURN_IF_WRITE_FAILED(ret);

		return 0;
	}
};

#endif // !__SYSTEM_RECORDER_HPP__
