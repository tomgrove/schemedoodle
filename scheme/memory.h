#pragma once

#include <stdint.h>
#include "schemetypes.h"
#include "context.h"
#include "collectable.h"

class Memory
{
	const static uint32_t cMaxCells = 1000000;
	const static uint32_t cMaxContexts = 1000;

	CellRef	   mCellFreeList;
	CellRef	   mCellAllocList;

	Context	   mRootContext;
	Context*   mContextFreeList;
	Context*   mContextAllocList;
public:
	Memory();
	Context* allocContext(Context* current, Context* outer);
	Context* allocContext(Context* current, Item variables, Cell* params, Context* outer);
	Cell*	 allocCell(Context* current, Item car, Item cdr = (CellRef)nullptr);
	void     gc(Context* context);
private:
	//void markCell(Cell* cell);
	//void markContext(Context* context);
	uint32_t mark(ICollectable* collectables);
	uint32_t collect(ICollectable** alloclist, ICollectable** freelist);
	//template< class T> T* makeFreeList(size_t size);
};
