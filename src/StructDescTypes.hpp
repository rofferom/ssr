#ifndef __STRUCTDESCTYPES_HPP__
#define __STRUCTDESCTYPES_HPP__

#include <string.h>
#include <assert.h>
#include <arpa/inet.h>

#include <list>

enum RawValueType : uint8_t {
	RAW_VALUE_TYPE_U8 = 0,
	RAW_VALUE_TYPE_I8,
	RAW_VALUE_TYPE_U16,
	RAW_VALUE_TYPE_I16,
	RAW_VALUE_TYPE_U32,
	RAW_VALUE_TYPE_I32,
	RAW_VALUE_TYPE_U64,
	RAW_VALUE_TYPE_I64,
	RAW_VALUE_TYPE_STR,

	RAW_VALUE_TYPE_INVALID
};

template <typename T>
struct ValueTrait {
	static constexpr RawValueType type = RAW_VALUE_TYPE_INVALID;
};

template <>
struct ValueTrait<uint8_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_U8;

	static ssize_t write(ISink *sink, const uint8_t &v)
	{
		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<int8_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_I8;

	static ssize_t write(ISink *sink, const int8_t &v)
	{
		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<uint16_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_U16;

	static ssize_t write(ISink *sink, const uint16_t &i)
	{
		uint16_t v = htons(i);

		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<int16_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_I16;

	static ssize_t write(ISink *sink, const int16_t &i)
	{
		uint16_t *t = (uint16_t *) &i;
		uint16_t v = htons(*t);

		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<uint32_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_U32;

	static ssize_t write(ISink *sink, const uint32_t &i)
	{
		uint32_t v = htonl(i);

		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<int32_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_I32;

	static ssize_t write(ISink *sink, const int32_t &i)
	{
		uint32_t *t = (uint32_t *) &i;
		uint32_t v = htonl(*t);

		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<uint64_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_U64;

	static ssize_t write(ISink *sink, const uint64_t &i)
	{
		uint64_t v = ((uint64_t) htonl(i & 0xFFFFFFFF) << 32) | htonl(i >> 32);

		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<int64_t> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_I64;

	static ssize_t write(ISink *sink, const int64_t &i)
	{
		uint64_t *t = (uint64_t *) &i;
		uint64_t v = ((uint64_t) htonl(*t & 0xFFFFFFFF) << 32) | htonl(*t >> 32);

		return sink->write(&v, sizeof(v));
	}
};

template <>
struct ValueTrait<std::string> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_STR;

	static ssize_t write(ISink *sink, const std::string &s)
	{
		size_t len = s.size();
		ssize_t written;
		ssize_t ret;

		if (len > 0xFFFF - 1) {
			errno = EINVAL;
			return -1;
		}

		// Write length
		uint16_t u16Len = (uint16_t) len + 1;
		ret = ValueTrait<uint16_t>::write(sink, u16Len);
		if (ret == -1)
			return ret;

		written = ret;

		// Write string content
		ret = sink->write(s.c_str(), len + 1);
		if (ret == -1)
			return ret;

		written += ret;
		return written;
	}
};

template <>
struct ValueTrait<const char *> {
	static constexpr RawValueType type = RAW_VALUE_TYPE_STR;

	static ssize_t write(ISink *sink, const char *v)
	{
		int len = strlen(v);
		ssize_t written;
		ssize_t ret;

		if (len > 0xFFFF - 1) {
			errno = EINVAL;
			return -1;
		}

		// Write length
		uint16_t u16Len = (uint16_t) len + 1;
		ret = ValueTrait<uint16_t>::write(sink, u16Len);
		if (ret == -1)
			return ret;

		written = ret;

		// Write string content
		ret = sink->write(v, len + 1);
		if (ret == -1)
			return ret;

		written += ret;
		return written;
	}
};

#endif // !__STRUCTDESCTYPES_HPP__
