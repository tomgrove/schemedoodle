#pragma once

#include <map>
#include "schemetypes.h"
#include "collectable.h"

extern bool gTrace;
extern bool gVerboseGC;
extern std::string print(Item);

struct Context : public Collectable<Context>
{
	std::map< Symbol, Item >	mBindings;
	Context*					mOuter;

	Context();

	Context(Context* context);

	Context(Item variables, CellRef params, Context* outer);

	Item Lookup(Symbol symbol);

	void Set(uint32_t symbol, Item value);

	void mark() override;

};

