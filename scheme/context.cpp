#include "stdafx.h"
#include <assert.h>
#include <sstream>
#include <string>
#include <map>
#include "schemetypes.h"
#include "collectable.h"
#include "context.h"
#include "symboltable.h"

extern bool gTrace;
extern bool gVerboseGC;
extern SymbolTable gSymbolTable;

extern std::string print(Item);

Context::Context()
	: mOuter(nullptr)
	, mBindings()
{}

Context::Context(Context* context)
		: mOuter(context)
{}

Context::Context(Item variables, CellRef params, Context* outer)
		: mOuter(outer)
{
	// variable is symbol: any number of arguments; arguments bound to symbol
	if (variables.type() == eSymbol)
	{
		mBindings[boost::any_cast<Symbol>(variables)] = params;
	}
	else
	{
		// variable is list
		assert(variables.type() == eCell);
		CellRef variablelist = boost::any_cast<CellRef>(variables);
		while (variablelist) {
			if (gTrace)
			{
				std::stringstream sstream;
				sstream << "Binding: " << print(variablelist->mCar) << " = " << print(params->mCar) << std::endl;
				puts(sstream.str().c_str());
			}
			mBindings[boost::any_cast<Symbol>(variablelist->mCar)] = params->mCar;

			if (variablelist->mCdr.type() == eCell)
			{
				variablelist = boost::any_cast<CellRef>(variablelist->mCdr);
				params = boost::any_cast<CellRef>(params->mCdr);
			}
			// pattern matching
			else
			{
				assert(variablelist->mCdr.type() == eSymbol);
				mBindings[boost::any_cast<Symbol>(variablelist->mCdr)] = params->mCdr;
				return;
			}
		}
	}
}

Item Context::Lookup(Symbol symbol)
{
	if (mBindings.find(symbol) == mBindings.end())
	{
		if (mOuter)
		{
			return mOuter->Lookup(symbol);
		}
		else
		{
			return Unspecified();
		}
	}
	return mBindings[symbol];
}

void Context::Set(uint32_t symbol, Item value)
{
	mBindings[symbol] = value;
}

void Context::mark()
{
	if ( mReachable)
	{
		return;
	}

	mReachable = true;

	for (auto pair : mBindings)
	{
		if (gVerboseGC)
		{
			printf("(%x) marking %s\n", this, gSymbolTable.GetString(pair.first).c_str());
		}
		if (pair.second.type() == eCell && boost::any_cast<CellRef>(pair.second))
		{
			auto cell = boost::any_cast<CellRef>(pair.second);
			cell->mark();
		}
		else if (pair.second.type() == eProc)
		{
			if (!boost::any_cast<Proc>(pair.second).mNative)
			{
				auto cell = boost::any_cast<Proc>(pair.second).mProc;
				cell->mark();
			}
			if (boost::any_cast<Proc>(pair.second).mClosure)
			{
				auto cell = boost::any_cast<Proc>(pair.second).mClosure;
				cell->mark();
			}
		}
	}

	if ( mOuter)
	{
		mOuter->mark();
	}
}

