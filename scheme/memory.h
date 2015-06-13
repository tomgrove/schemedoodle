#pragma once

#include <stdint.h>
#include <memory>
#include "schemetypes.h"
#include "context.h"
#include "collectable.h"

class Memory
{
	const static uint32_t cMaxCells = 1000000;
	const static uint32_t cMaxContexts = 1000;

	Freelist<Cell>			mCells;
	Freelist<Context>		mContexts;
	Context*				mRootContext;
public:
	Memory();
	Context* allocContext(Context* current, Context* outer);
	Context* allocContext(Context* current, Item variables, Cell* params, Context* outer);
	Cell*	 allocCell(Context* current, Item car, Item cdr = (CellRef)nullptr);
	void     gc(Context* context);
	Context* getRoot() { return mRootContext;  }
private:
	//uint32_t mark(ICollectable* collectables);
	//uint32_t collect(ICollectable** alloclist, ICollectable** freelist);
};
