#pragma once

#include <stdint.h>
#include <functional>
#include "collectable.h"
#include "boost\any.hpp"

struct Context;

typedef boost::any Item;
typedef std::function<void(Item)>  Continuation;
typedef std::function<void(Item, Context*, Continuation)> Native;

struct Cell;

struct Proc {
	Cell*		mProc;
	Context*	mClosure;
	Native		mNative;
	Proc(Native native)
		: mNative(native)
		, mProc(nullptr)
		, mClosure(nullptr)
	{}
	Proc(Cell* proc, Context* closure)
		: mNative(nullptr)
		, mProc(proc)
		, mClosure(closure)
	{}
	bool operator==(Proc& rhs) const
	{
		if (rhs.mNative) {
			return false;
		}
		return this->mProc == rhs.mProc;
	}
};

struct Unspecified {};

typedef int32_t					Number;
typedef Cell*					CellRef;
typedef uint32_t				Symbol;

const type_info& eUnspecified	= typeid(Unspecified);
const type_info& eSymbol		= typeid(Symbol);
const type_info& eNumber		= typeid(Number);
const type_info& eCell			= typeid(CellRef);
const type_info& eProc			= typeid(Proc);

struct Cell : public Collectable<Cell>
{
	Item		mCar;
	Item		mCdr;
	Cell()
		: mCdr()
		, mCar()
	{}
	Cell(Item car, Item cdr)
		: mCar(car)
		, mCdr(cdr)
	{}
	Cell(Item car)
		: mCar(car)
		, mCdr((CellRef)nullptr)
	{}
};
