#ifndef __STRUCTDESCREGISTRY_HPP__
#define __STRUCTDESCREGISTRY_HPP__

#include <list>
#include "Log.hpp"
#include "StructDesc.hpp"

#define STRUCT_DESC_REGISTRY_INVALID_ID -1

class StructDescRegistry {
public:
	// Generic type description
	struct Type {
		int mId;
		std::string mName;
		StructDesc mDesc;

		Type()
		{
			mId = STRUCT_DESC_REGISTRY_INVALID_ID;
		}
	};

private:
	// id allocator
	static int sNextId;

	// Entry storage + type <-> Entry mapping
	template <typename T>
	struct EntryMap {
		static Type entry;
	};

	// Entry enumeration
	static std::list<Type *> sEntryList;

public:
	template <typename T>
	static int registerType(const char *name, StructDesc **desc)
	{
		int id = EntryMap<T>::entry.mId;

		if (!desc)
			return -EINVAL;

		if (id != STRUCT_DESC_REGISTRY_INVALID_ID) {
			LOGW("Type '%s' already registered", name);
			return -EPERM;
		}

		EntryMap<T>::entry.mId = sNextId++;
		EntryMap<T>::entry.mName = name;

		sEntryList.push_back(&EntryMap<T>::entry);

		*desc = &EntryMap<T>::entry.mDesc;
		LOGD("Type %s registered", name);

		return 0;
	}

	template <typename T>
	static int getType(const Type **type)
	{
		int id = EntryMap<T>::entry.mId;

		if (!type)
			return -EINVAL;

		if (id == STRUCT_DESC_REGISTRY_INVALID_ID) {
			LOGE("Trying to get an unknown type");
			return id;
		}

		*type = &EntryMap<T>::entry;

		return 0;
	}

	static int getTypeList(const std::list<Type *> **typeList)
	{
		if (!typeList)
			return -EINVAL;

		*typeList = &sEntryList;

		return 0;
	}
};

template <typename T>
StructDescRegistry::Type StructDescRegistry::EntryMap<T>::entry;

#endif // !__STRUCTDESCREGISTRY_HPP__
