#include "stdafx.h"
#include "schemetypes.h"

const type_info& eUnspecified = typeid(Unspecified);
const type_info& eSymbol = typeid(Symbol);
const type_info& eNumber = typeid(Number);
const type_info& eCell = typeid(CellRef);
const type_info& eProc = typeid(Proc);

Number Cell::length()
{
	auto cell = boost::any_cast<CellRef>(mCdr);
	if (cell)
	{
		return 1 + cell->length();
	}
	else
	{
		return 1;
	}
}

void Cell::mark()
{
	if ( mReachable)
	{
		return;
	}

	mReachable = true;
	if ( mCar.type() == eCell && boost::any_cast<CellRef>(mCar))
	{
		auto cell = boost::any_cast<CellRef>(mCar);
		cell->mark();
	}

	if ( mCdr.type() == eCell && boost::any_cast<CellRef>(mCdr))
	{
		auto cell = boost::any_cast<CellRef>(mCdr);
		cell->mark();
	}
}