#include "stdafx.h"
#include <assert.h>
#include <sstream>
#include <string>
#include <map>
#include "schemetypes.h"
#include "collectable.h"
#include "context.h"

extern bool gTrace;
extern bool gVerboseGC;
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
