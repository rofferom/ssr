#include "ssr_priv.hpp"

template <>
ssize_t StructDesc::valueWriterRaw<const char *>(EntryDesc *desc, ISink *sink, void *base)
{
	std::ptrdiff_t p = (std::ptrdiff_t ) base + desc->mParams.raw.offset;
	return ValueTrait<const char *>::write(sink, (const char *) p);
}
