#ifndef __SYSTEM_RECORDER_HPP__
#define __SYSTEM_RECORDER_HPP__

class SystemRecorder {
private:
	FILE *mFile;
	FileSink *mSink;

private:
	int writeHeader();

public:
	SystemRecorder();
	~SystemRecorder();

	int open(const char *path);
	int close();

	int flush();

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

	static int registerStructDescs();
};

#endif // !__SYSTEM_RECORDER_HPP__
