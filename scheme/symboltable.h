#pragma once

#include <map>
#include "schemetypes.h"

struct SymbolTable {
	std::map< std::string, Symbol>	mStringToSymbol;
	std::map< Symbol, std::string > mSymbolToString;
	uint32_t						mCount;

	SymbolTable();
	Symbol GetSymbol(std::string symbol);
	std::string GetString(Symbol symbol);
};