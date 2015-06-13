#include "stdafx.h"
#include <assert.h>
#include "stdint.h"
#include "parser.h"
#include "memory.h"
#include "symboltable.h"
#include "list.h"

extern Memory	    gMemory;
extern SymbolTable  gSymbolTable;

static bool isSign(char c)
{
	return c == '-';
}

static bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static Number parseDigits(char*cs, char** rest, Number previous)
{
	if (!isDigit(*cs))
	{
		*rest = cs;
		return previous;
	}
	else
	{
		Number digit = *cs - '0';
		cs++;
		return parseDigits(cs, rest, previous * 10 + digit);
	}
}

static Maybe<Item> parseNumber(char* cs, char** rest)
{
	int32_t sign = 1;
	if (isSign(*cs))
	{
		sign = -1;
		cs++;
	}

	if (!isDigit(*cs))
	{
		return Maybe<Item>();
	}

	return Maybe<Item>(Item(parseDigits(cs, rest, 0) * sign));
}

static bool isSymbolInitial(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '+' || c == '=' || c == '*' || c == '/' || c == '-' || c == '%';
}

static bool isSymbolBody(char c)
{
	return isSymbolInitial(c) || isDigit(c) || (c == '-') || (c == '?') || (c == '!');
}

static Maybe<Item> parseSymbol(char* cs, char** rest)
{
	if (isSymbolInitial(*cs))
	{
		std::string symbol;
		while (isSymbolBody(*cs))
		{
			symbol.push_back(*cs);
			cs++;
		}
		*rest = cs;
		return Maybe<Item>(Item(gSymbolTable.GetSymbol(symbol)));
	}
	else
	{
		return Maybe<Item>();
	}
}

static bool isNewline(char c)
{
	return (c == '\n') || (c == '\f') || (c == '\r');
}

static bool isWhitespace(char c)
{
	return (c == ' ') || (c == '\t') || isNewline(c);
}

static Maybe<void> parseWhitespace(char* cs, char** rest)
{
	Maybe<void> valid(true);
	while (isWhitespace(*cs))
	{
		cs++;
	}

	*rest = cs;
	return valid;
}

static Maybe<void> parseSingleLineComment(char* cs, char** rest)
{
	if (*cs != ';')
	{
		return Maybe<void>();
	}
	cs++;
	while (!isNewline(*cs) && *cs != '\0')
	{
		cs++;
	}
	*rest = cs;

	return Maybe<void>(true);
}

static Maybe<void> parseAtmosphere(char* cs, char** rest)
{
	if (*cs == ';')
	{
		parseSingleLineComment(cs, rest);
		return parseAtmosphere(*rest, rest);
	}
	else if (isWhitespace(*cs))
	{
		parseWhitespace(cs, rest);
		return parseAtmosphere(*rest, rest);
	}
	else
	{
		*rest = cs;
	}

	return Maybe<void>(true);
}

static Maybe<Item> parseList(Context* context, char* cs, char** rest);

static Maybe<Item> parseQuotedForm(Context* context, char*cs, char** rest)
{
	if (*cs != '\'')
	{
		return Maybe<Item>();
	}
	cs++;

	Maybe<Item> item;
	if ((item = Parser::parseForm(context, cs, rest)).mValid)
	{
		Cell* qcell = gMemory.allocCell(context, Item(gSymbolTable.GetSymbol("quote")), Item(gMemory.allocCell(context, item.mV)));
		return Maybe<Item>(Item(qcell));
	}

	return Maybe<Item>();
}

static Maybe<Item> parseUnquotedForm(Context* context, char*cs, char** rest)
{
	Maybe<Item> item;
	if ((item = parseNumber(cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parseSymbol(cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parseList(context, cs, rest)).mValid)
	{
		return item;
	}
	else
	{
		return Maybe<Item>();
	}
}

Maybe<Item> Parser::parseForm(Context* context, char* cs, char** rest)
{
	Maybe<Item> item;
	if ((item = parseQuotedForm(context, cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parseUnquotedForm(context, cs, rest)).mValid)
	{
		return item;
	}
	return Maybe<Item>();
}

static Maybe<Item> parsePair(Context* context, char* cs, char** rest)
{
	if (*cs != '.')
	{
		return Maybe<Item>();
	}

	cs++;
	parseAtmosphere(cs, rest);
	cs = *rest;

	Maybe<Item> item;
	if ((item = Parser::parseForm(context, cs, rest)).mValid)
	{
		return item;
	}

	return Maybe<Item>();
}

static Maybe<Cell*> parseForms(Context* context, char* cs, char** rest)
{
	Maybe<Item> item;
	Cell* cell = nullptr;
	if ((item = Parser::parseForm(context, cs, rest)).mValid)
	{
		cell = gMemory.allocCell(context, Item());
		cell->mCar = item.mV;
	}
	else
	{
		return Maybe<Cell*>();
	}

	parseAtmosphere(*rest, rest);
	cs = *rest;

	Maybe<Item> second;
	if ((second = parsePair(context, cs, rest)).mValid)
	{
		cell->mCdr = second.mV;
	}
	else
	{
		Maybe<Cell*> tail;
		if ((tail = parseForms(context, cs, rest)).mValid)
		{
			cell->mCdr = Item(tail.mV);
		}
		else
		{
			cell->mCdr = Item((Cell*)nullptr);
		}
	}

	return Maybe<Cell*>(cell);
}

static Maybe<Item> parseNil(char* cs, char** rest)
{
	if (*cs == '(')
	{
		cs++;
	}
	else
	{
		return Maybe<Item>();
	}

	parseAtmosphere(cs, rest);

	cs = *rest;

	if (*cs == ')')
	{
		cs++;
		*rest = cs;
		return Maybe<Item>(Item((Cell*)nullptr));
	}

	return Maybe<Item>();
}

static Maybe<Item> parseNonEmptyList(Context* context, char* cs, char**rest)
{
	Maybe<Item> item;
	if (*cs != '(')
	{
		return Maybe<Item>();
	}
	cs++;

	parseAtmosphere(cs, rest);
	Maybe<Cell*> cell;
	if ((cell = parseForms(context, *rest, rest)).mValid)
	{
		item = Item(cell.mV);
	}
	else
	{
		return Maybe<Item>();
	}
	parseAtmosphere(*rest, rest);

	cs = *rest;
	if (*cs != ')')
	{
		return Maybe<Item>();
	}
	cs++;
	*rest = cs;

	return Maybe<Item>(item);
}


static Maybe<Item> parseList(Context* context, char* cs, char** rest)
{
	Maybe<Item> item;
	if ((item = parseNil(cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parseNonEmptyList(context, cs, rest)).mValid)
	{
		return item;
	}
	return Maybe<Item>();
}

void test_numbers()
{
	char* rest;
	assert(boost::any_cast<Number>((parseNumber("10", &rest)).mV) == 10);
	assert(boost::any_cast<Number>((parseNumber("0", &rest)).mV) == 0);
	assert(boost::any_cast<Number>((parseNumber("-0", &rest)).mV) == 0);
	assert((parseNumber("", &rest)).mValid == false);
	assert(boost::any_cast<Number>((parseNumber("100", &rest)).mV) == 100);
	assert(boost::any_cast<Number>((parseNumber("-123", &rest)).mV) == -123);
	assert((parseNumber("cat", &rest)).mValid == false);
}

void test_comments()
{
	char* rest;
	assert(parseSingleLineComment("; this is a comment\n", &rest).mValid);
	assert(!parseSingleLineComment("this is not a comment\n", &rest).mValid);
	assert(parseSingleLineComment(";this is a comment", &rest).mValid);
}

void test_atmosphere()
{
	char* rest;
	assert(parseAtmosphere("; this is a comment\n  ; so is this\n    something", &rest).mValid == true);
	assert(std::string(rest) == "something");
}

void test_symbols()
{
	char* rest;

	SymbolTable symbols;
	auto id0 = symbols.GetSymbol("cat");
	auto id1 = symbols.GetSymbol("cat");
	assert(id0 == id1);

	assert(symbols.GetString(id0) == "cat");

	assert(gSymbolTable.GetString(boost::any_cast<Symbol>(parseSymbol("cat", &rest).mV)) == "cat");
	assert(*rest == '\0');

	assert(gSymbolTable.GetString(boost::any_cast<Symbol>(parseSymbol("this-is-a-symbol", &rest).mV)) == "this-is-a-symbol");
	assert(parseSymbol("0this-is-not-a-symbol", &rest).mValid == false);
	assert(gSymbolTable.GetString(boost::any_cast<Symbol>(parseSymbol("several symbols", &rest).mV)) == "several");
	assert(gSymbolTable.GetString(boost::any_cast<Symbol>(parseSymbol("UniCorn5", &rest).mV)) == "UniCorn5");
}

void test_list()
{
	char* rest;
	assert(parseList(gMemory.getRoot(), "()", &rest).mValid);
	assert(parseList(gMemory.getRoot(), "(    )", &rest).mValid);
	assert(parseList(gMemory.getRoot(), "()", &rest).mV.type() == eCell);
	assert(parseList(gMemory.getRoot(), "( cat )", &rest).mV.type() == eCell);
	assert(parseList(gMemory.getRoot(), "( cat vs unicorn )", &rest).mV.type() == eCell);
	assert(parseList(gMemory.getRoot(), "( Imogen loves Mummy and Daddy . xs )", &rest).mV.type() == eCell);
	assert(parseList(gMemory.getRoot(), "( first . second )", &rest).mV.type() == eCell);
	assert(parseList(gMemory.getRoot(), "( lambda (x) ( plus x 10 ) )", &rest).mV.type() == eCell);

	assert(length(boost::any_cast<CellRef>(parseList(gMemory.getRoot(), "( cat 100 unicorn )", &rest).mV)) == 3);
	assert(length(boost::any_cast<CellRef>(parseList(gMemory.getRoot(), "( Maddy loves ( horses and unicorns) )", &rest).mV)) == 3);
	assert(length(boost::any_cast<CellRef>(parseList(gMemory.getRoot(), "( Maddy loves ; inject a comment \n( horses and unicorns) )", &rest).mV)) == 3);
	assert(!parseList(gMemory.getRoot(), "( Maddy loves ", &rest).mValid);
}


void test_quote()
{
	char* rest;
	assert(Parser::parseForm(gMemory.getRoot(), "'hello-everyone", &rest).mValid);
	assert(Parser::parseForm(gMemory.getRoot(), "'(hello everyone)", &rest).mValid);
	assert(Parser::parseForm(gMemory.getRoot(), "'''hello-multiqoute", &rest).mValid);
}

void Parser::test()
{
	test_numbers();
	test_symbols();
	test_comments();
	test_atmosphere();
	test_list();
	test_quote();
}