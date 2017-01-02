#ifndef __STRUCTDESC_HPP__
#define __STRUCTDESC_HPP__

#include <string>
#include <memory>
#include "FileSink.hpp"
#include "StructDescTypes.hpp"

#define offsetof(type, member)  __builtin_offsetof (type, member)

#define REGISTER_RAW_VALUE(desc, _class_, field, name) \
	(desc)->registerRawValue<decltype(_class_::field)>( \
			name, offsetof(_class_, field))

#define REGISTER_STRING(desc, _class_, field, name) \
	(desc)->registerRawValue<const char *>( \
			name, offsetof(_class_, field))

class StructDesc {
private:
	enum EntryType : uint8_t {
		ENTRY_TYPE_RAWVALUE = 0,
	};

	struct EntryDesc {
		std::string mName;

		ssize_t (*mDescWriter) (EntryDesc *desc, ISink *sink);
		ssize_t (*mValueWriter) (EntryDesc *desc, ISink *sink, void *base);

		EntryDesc()
		{
			mDescWriter = nullptr;
			mValueWriter = nullptr;
		}

		union {
			struct {
				uint64_t offset;
			} raw;
		} mParams;
	};

//	TODO : try to restore this for gcc versions >= 4.7
//	template <typename T>
//	using EnumType = typename std::underlying_type<T>::type;

private:
	std::list<EntryDesc *> mEntryDescList;

private:
	template <typename T>
	static ssize_t descWriterRaw(EntryDesc *desc, ISink *sink)
	{
		ValueTrait<std::string>::write(sink, desc->mName);

		uint8_t type = EntryType::ENTRY_TYPE_RAWVALUE;
		ValueTrait<uint8_t>::write(sink, type);

		uint8_t rawType = ValueTrait<T>::type;
		ValueTrait<uint8_t>::write(sink, rawType);

		return 0;
	}

	template <typename T>
	static ssize_t valueWriterRaw(EntryDesc *desc, ISink *sink, void *base)
	{
		std::ptrdiff_t p = (std::ptrdiff_t ) base + desc->mParams.raw.offset;
		return ValueTrait<T>::write(sink, *((T *) p));
	}

public:
	~StructDesc()
	{
		for (auto &e : mEntryDescList)
			delete e;
	}

	template <typename T>
	int registerRawValue(const char *name, uint64_t offset)
	{
		EntryDesc *desc = new EntryDesc();

		static_assert(ValueTrait<T>::type != RAW_VALUE_TYPE_INVALID,
			      "Unsupported type");

		desc->mName = name;

		desc->mDescWriter = descWriterRaw<T>;
		desc->mValueWriter = valueWriterRaw<T>;
		desc->mParams.raw.offset = offset;

		mEntryDescList.push_back(desc);

		return 0;
	}

	int writeDesc(ISink *sink)
	{
		uint32_t entryCount = (uint32_t) mEntryDescList.size();
		ValueTrait<uint32_t>::write(sink, entryCount);

		for (auto &desc: mEntryDescList)
			desc->mDescWriter(desc, sink);

		return 0;
	}

	int writeValueInternal(ISink *sink, void *p)
	{
		for (auto &desc: mEntryDescList)
			desc->mValueWriter(desc, sink, p);

		return 0;
	}

	template <typename T>
	int writeValue(ISink *sink, T *p)
	{
		return writeValueInternal(sink, (void *) p);
	}
};

template <>
ssize_t StructDesc::valueWriterRaw<const char *>(EntryDesc *desc, ISink *sink, void *base)
{
	std::ptrdiff_t p = (std::ptrdiff_t ) base + desc->mParams.raw.offset;
	return ValueTrait<const char *>::write(sink, (const char *) p);
}

#endif // !__STRUCTDESC_HPP__
