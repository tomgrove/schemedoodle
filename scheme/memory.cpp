#include "stdafx.h"
#include <stdint.h>
#include <assert.h>
#include "schemetypes.h"
#include "context.h"
#include "memory.h"

Memory::Memory()
	: mContexts( cMaxContexts )
	, mCells( cMaxCells )
{
	mRootContext = mContexts.alloc(nullptr);
}

Context* Memory::allocContext(Context* current, Context* outer)
{
	Context* context = mContexts.alloc( outer);
	if (!context)
	{
		gc(current);
		Context* context = mContexts.alloc( outer);
		assert(context);
	}

	return context;
}

Context* Memory::allocContext(Context* current, Item variables, Cell* params, Context* outer)
{
	Context* context = mContexts.alloc( variables, params, outer );
	if (!context)
	{
		gc(current);
		Context* context = mContexts.alloc(variables, params, outer);
		assert(context);
	}

	return context;
}

Cell* Memory::allocCell(Context* current, Item car, Item cdr )
{
	Cell* cell = mCells.alloc(car,cdr);
	if (!cell)
	{
		gc(current);
		cell = mCells.alloc(car,cdr);
		assert(cell);
	}

	return cell;
}

void Memory::gc(Context* context)
{
	uint32_t cellcount		= mCells.markUnreachable();
	uint32_t contextcount	= mContexts.markUnreachable();

	if (gVerboseGC)
	{
		printf("considering %d cells and %d contexts during GC\n", cellcount, contextcount);
	}

	context->mark();

	uint32_t gc_cellcount	 = mCells.collect();
	uint32_t gc_contextcount = mContexts.collect();

	if (gVerboseGC)
	{
		printf("return %d cells and %d contexts to the free lists\n", gc_cellcount, gc_contextcount);
	}
}
