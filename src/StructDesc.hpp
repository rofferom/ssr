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

#define REGISTER_LIST(desc, _class_, field, name, elemDesc) \
	(desc)->registerList<decltype(_class_::field)>( \
			name, offsetof(_class_, field), elemDesc);

#define REGISTER_STRUCT(desc, _class_, field, name, elemDesc) \
	(desc)->registerStruct(name, offsetof(_class_, field), elemDesc);

class StructDesc {
private:
	enum EntryType : uint8_t {
		ENTRY_TYPE_RAWVALUE = 0,
		ENTRY_TYPE_STRUCT,
		ENTRY_TYPE_LIST
	};

	struct EntryDesc {
		std::string mName;
		std::shared_ptr<StructDesc> mChildDesc;

		std::function<ssize_t(ISink *sink)> mDescWriter;
		std::function<ssize_t(ISink *sink, void *base)> mValueWriter;

		EntryDesc()
		{
			mChildDesc = nullptr;
			mDescWriter = nullptr;
			mValueWriter = nullptr;
		}
	};

//	TODO : try to restore this for gcc versions >= 4.7
//	template <typename T>
//	using EnumType = typename std::underlying_type<T>::type;

private:
	std::list<EntryDesc *> mEntryDescList;

public:
	~StructDesc()
	{
		for (auto &e : mEntryDescList) {
			e->mChildDesc.reset();
			delete e;
		}
	}

	template <typename T>
	int registerRawValue(const char *name, uint64_t offset)
	{
		EntryDesc *desc = new EntryDesc();

		static_assert(ValueTrait<T>::type != RAW_VALUE_TYPE_INVALID,
			      "Unsupported type");

		desc->mName = name;

		desc->mDescWriter = [desc] (ISink *sink) -> ssize_t {
			ValueTrait<std::string>::write(sink, desc->mName);

			uint8_t type = EntryType::ENTRY_TYPE_RAWVALUE;
			ValueTrait<uint8_t>::write(sink, type);

			uint8_t rawType = ValueTrait<T>::type;
			ValueTrait<uint8_t>::write(sink, rawType);

			return 0;
		};

		desc->mValueWriter = [offset] (ISink *sink, void *base) -> ssize_t {
			std::ptrdiff_t p = (std::ptrdiff_t ) base + offset;
			return ValueTrait<T>::write(sink, *((T *) p));
		};

		mEntryDescList.push_back(desc);

		return 0;
	}

	int registerStruct(const char *name,
			   uint64_t offset,
			   std::shared_ptr<StructDesc> structDesc)
	{
		EntryDesc *desc = new EntryDesc();

		desc->mName = name;
		desc->mChildDesc = structDesc;

		desc->mDescWriter = [desc, structDesc] (ISink *sink) -> ssize_t {
			ValueTrait<std::string>::write(sink, desc->mName);

			uint8_t type = EntryType::ENTRY_TYPE_STRUCT;
			ValueTrait<uint8_t>::write(sink, type);

			uint32_t entryCount = (uint32_t) structDesc->mEntryDescList.size();
			ValueTrait<uint32_t>::write(sink, entryCount);

			for (auto &desc: structDesc->mEntryDescList)
				desc->mDescWriter(sink);

			return 0;
		};

		desc->mValueWriter = [offset, structDesc] (ISink *sink, void *base) -> ssize_t {
			std::ptrdiff_t p = (std::ptrdiff_t) base + offset;
			structDesc->writeValueInternal(sink, (void *) p);
			return 0;
		};

		mEntryDescList.push_back(desc);

		return 0;
	}

	template <typename T>
	int registerList(const char *name,
			 uint64_t offset,
			 std::shared_ptr<StructDesc> itemDesc)
	{
		EntryDesc *desc = new EntryDesc();

		desc->mName = name;
		desc->mChildDesc = itemDesc;

		desc->mDescWriter = [desc, itemDesc] (ISink *sink) -> ssize_t {
			ValueTrait<std::string>::write(sink, desc->mName);

			uint8_t type = EntryType::ENTRY_TYPE_LIST;
			ValueTrait<uint8_t>::write(sink, type);

			uint32_t entryCount = (uint32_t) itemDesc->mEntryDescList.size();
			ValueTrait<uint32_t>::write(sink, entryCount);

			for (auto &desc: itemDesc->mEntryDescList)
				desc->mDescWriter(sink);

			return 0;
		};

		desc->mValueWriter = [offset, itemDesc] (ISink *sink, void *base) -> ssize_t {
			T *t = (T *) ((std::ptrdiff_t ) base + offset);
			uint32_t count = (uint32_t) t->size();
			ssize_t written;

			written = ValueTrait<uint32_t>::write(sink, count);
			for (auto &val : *t)
				itemDesc->writeValueInternal(sink, (void * )&val);

			return written;
		};

		mEntryDescList.push_back(desc);

		return 0;
	}

	int writeDesc(ISink *sink)
	{
		uint32_t entryCount = (uint32_t) mEntryDescList.size();
		ValueTrait<uint32_t>::write(sink, entryCount);

		for (auto &desc: mEntryDescList)
			desc->mDescWriter(sink);

		return 0;
	}

	int writeValueInternal(ISink *sink, void *p)
	{
		for (auto &desc: mEntryDescList)
			desc->mValueWriter(sink, p);

		return 0;
	}

	template <typename T>
	int writeValue(ISink *sink, T *p)
	{
		return writeValueInternal(sink, (void *) p);
	}
};

#endif // !__STRUCTDESC_HPP__
