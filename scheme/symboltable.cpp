#include "stdafx.h"
#include "symboltable.h"

SymbolTable::SymbolTable() 
	: mCount(0)
{}

Symbol SymbolTable::GetSymbol(std::string symbol)
{
	if (mStringToSymbol.find(symbol) == mStringToSymbol.end())
	{
		mStringToSymbol[symbol] = mCount++;
		mSymbolToString[mCount - 1] = symbol;
	}

	return mStringToSymbol[symbol];
}

std::string SymbolTable::GetString(Symbol symbol)
{
	if (mSymbolToString.find(symbol) != mSymbolToString.end())
	{
		return mSymbolToString[symbol];
	}

	return "";
}
