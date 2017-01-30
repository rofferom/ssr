#include "ssr_priv.hpp"

int StructDescRegistry::sNextId = 0;

std::list<StructDescRegistry::Type *> StructDescRegistry::sEntryList;
