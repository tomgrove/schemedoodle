#include "stdafx.h"
#include <stdint.h>
#include <assert.h>
#include "schemetypes.h"
#include "context.h"
#include "memory.h"

template< class T>
static T* makeFreeList(size_t size)
{
	T* freelist = new T[size];
	for (uint32_t i = 0; i < size - 1; i++)
	{
		freelist[i].mNext = &freelist[i + 1];
	}
	return freelist;
}

Memory::Memory()
{
	mCellFreeList = makeFreeList<Cell>(cMaxCells);
	mContextFreeList = makeFreeList<Context>(cMaxContexts);
	mCellAllocList = nullptr;
	mContextAllocList = &mRootContext;
}

Context* Memory::allocContext(Context* current, Context* outer)
{
	if (mContextFreeList == nullptr)
	{
		gc(current);
		assert(mContextFreeList);
	}

	auto next = mContextFreeList->mNext;
	auto context = new (mContextFreeList)Context(outer);
	mContextFreeList = next;
	context->mNext = mContextAllocList;
	mContextAllocList = context;
	return context;
}

Context* Memory::allocContext(Context* current, Item variables, Cell* params, Context* outer)
{
	if (mContextFreeList == nullptr)
	{
		gc(current);
		assert(mContextFreeList);
	}

	auto next = mContextFreeList->mNext;
	auto context = new (mContextFreeList)Context(variables, params, outer);
	mContextFreeList = next;
	context->mNext = mContextAllocList;
	mContextAllocList = context;
	return context;
}

Cell* Memory::allocCell(Context* current, Item car, Item cdr )
{
	if (mCellFreeList == nullptr)
	{
		gc(current);
		assert(mCellFreeList);;
	}

	auto next = mCellFreeList->mNext;
	auto cell = new (mCellFreeList)Cell(car, cdr);
	mCellFreeList = next;
	cell->mNext = mCellAllocList;
	mCellAllocList = cell;

	return cell;
}

uint32_t Memory::mark(ICollectable* collectables)
{
	uint32_t count = 0;
	ICollectable* collectable = collectables;
	while ( collectable )
	{
		collectable->setReachable( false);
		collectable = collectable->getNext();
		count++;
	}

	return count;
}

uint32_t Memory::collect(ICollectable** alloclist, ICollectable** freelist)
{
	ICollectable* i = *alloclist;
	ICollectable* prev = nullptr;
	uint32_t collected = 0;
	while (i)
	{
		ICollectable* next = i->getNext();
		if (!i->mReachable)
		{
			if (!prev)
			{
				*alloclist = i->getNext();
			}
			else
			{
				prev->setNext(  i->getNext()) ;
			}

			i->setNext( *freelist) ;
			*freelist = i;
			collected++;
		}
		else
		{
			prev = i;
		}

		i = next;
	}

	return collected;
}

void Memory::gc(Context* context)
{
	uint32_t cellcount = mark(mCellAllocList);
	uint32_t contextcount = mark(mContextAllocList);

	if (gVerboseGC)
	{
		printf("considering %d cells and %d contexts during GC\n", cellcount, contextcount);
	}

	context->mark();

	uint32_t gc_cellcount = collect( (ICollectable**)&mCellAllocList, (ICollectable**)&mCellFreeList);
	uint32_t gc_contextcount = collect((ICollectable**)&mContextAllocList, (ICollectable**)&mContextFreeList);

	if (gVerboseGC)
	{
		printf("return %d cells and %d contexts to the free lists\n", gc_cellcount, gc_contextcount);
	}
}
