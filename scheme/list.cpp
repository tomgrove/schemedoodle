#include "stdafx.h"
#include "schemetypes.h"
#include <assert.h>

uint32_t length(CellRef cell)
{
	if (cell == nullptr)
	{
		return 0;
	}
	else
	{
		return 1 + length(boost::any_cast<CellRef>(cell->mCdr));
	}
}

Item car(Item pair)
{
	assert(pair.type() == eCell && boost::any_cast<CellRef>(pair) != nullptr);
	return boost::any_cast<CellRef>(pair)->mCar;
}

Item cdr(Item pair)
{
	assert(pair.type() == eCell && boost::any_cast<CellRef>(pair) != nullptr);
	return boost::any_cast<CellRef>(pair)->mCdr;
}