// scheme.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include <map>
#include <assert.h>
#include <sstream>
#include <functional>

static bool gTrace = false;
static bool gVerboseGC = true;

template<typename T>
struct Maybe
{
	Maybe(T v)
		: mV(v)
		, mValid(true)
	{}

	Maybe()
		: mV()
		, mValid(false)
	{}

	T	 mV;
	bool mValid;
};

template<> struct Maybe<void>
{
	Maybe()
		: mValid(false)
	{}
	Maybe( bool valid )
		: mValid(valid)
	{}
	bool mValid;
};

struct SymbolTable {
	std::map< std::string, uint32_t> mSymbols;
	std::map< uint32_t, std::string > mReverse;
	uint32_t						 mCount;
	SymbolTable() : mCount(0)
	{}
	uint32_t GetSymbol(std::string symbol)
	{
		if (mSymbols.find(symbol) == mSymbols.end())
		{
			mSymbols[symbol] = mCount++;
			mReverse[mCount - 1] = symbol;
		}

		return mSymbols[symbol];
	}

	std::string GetString(uint32_t symbol)
	{
		if (mReverse.find(symbol) != mReverse.end())
		{
			return mReverse[symbol];
		}

		return "";
	}
};

static SymbolTable gSymbolTable;

struct Cell;
struct Context;

typedef enum Tag 
{
	eNumber,
	eSymbol,
	eCell,
	eProc,
	eUnspecified
};

struct Item;
typedef void (*Native)(Item,Context*, std::function<void(Item)>);

struct Proc {
	Cell*		mProc;
	Context*	mClosure;
	Native		mNative;
};

struct Item {
	union {
		int32_t		mNumber;
		uint32_t    mSymbol;
		Proc		mProc;
		Cell*		mCell;
	};
	Tag				mTag;
	explicit Item(int32_t number)
		: mNumber(number)
		, mTag(eNumber)
	{}
	explicit Item(std::string symbol)
		: mSymbol(gSymbolTable.GetSymbol(symbol))
		, mTag(eSymbol)
	{}
	explicit Item(Cell* cell)
		: mCell(cell)
		, mTag(eCell)
	{}
	Item(Cell * proc, Context* context)
		: mTag(eProc)
	{
		mProc.mProc = proc;
		mProc.mClosure = context;
		mProc.mNative = nullptr;
	}
	explicit Item(Native native)
		: mTag(eProc)
	{
		mProc.mProc = nullptr;
		mProc.mClosure = nullptr;
		mProc.mNative = native;
	}
	Item()
		: mTag(eUnspecified)
	{}
};

static std::string print(Item item);

struct Cell {
	Cell*		mNext;
	Item		mCar;
	Item		mCdr;
	bool		mReachable;
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
		, mCdr((Cell*)nullptr)
	{}
};

static Item gUnspecified;

struct Context;
void eval(Item item, Context* context, std::function<void(Item)> k);
void gc(Context*);

struct Context {
	std::map< uint32_t, Item >	mBindings;
	Context*					mOuter;
	Context*					mNext;
	bool						mReachable;

	Context()
		: mOuter(nullptr)
		, mBindings()
	{}

	Context(Context* context)
		: mOuter(context)
	{}

	Context(Cell* variables, Cell* params, Context* outer)
		: mOuter(outer)
	{
		std::stringstream sstream;
		while(variables) {
			if (gTrace)
			{
				sstream << "Binding: " << print(variables->mCar) << " = " << print(params->mCar) << std::endl;
				puts(sstream.str().c_str());
			}
			mBindings[variables->mCar.mSymbol] = params->mCar;
			variables	= variables->mCdr.mCell;
			params		= params->mCdr.mCell;
		}
	}

	Item Lookup(uint32_t symbol )
	{
		if (mBindings.find(symbol) == mBindings.end())
		{
			if (mOuter)
			{
				return mOuter->Lookup(symbol);
			}
			else
			{
				return gUnspecified;
			}
		}
		return mBindings[symbol];
	}

	void Set(uint32_t symbol, Item value)
	{
		mBindings[symbol] = value;
	}
};

const static uint32_t cMaxCells = 1000000;
const static uint32_t cMaxContexts = 1000;

static Cell*	   gCellFreeList;
static Cell*	   gCellAllocList;

static Context	   gRootContext;
static Context*	   gContextFreeList;
static Context*	   gContextAllocList;

void allocFreeLists()
{
	gCellFreeList = new Cell[ cMaxCells ];
	gContextFreeList = new Context[ cMaxContexts ];
	gCellAllocList = nullptr;
	gContextAllocList = &gRootContext;

	for (uint32_t i = 0; i < cMaxCells - 1; i++)
	{
		gCellFreeList[i].mNext = &gCellFreeList[i + 1];
	}

	gCellFreeList[cMaxCells - 1].mNext = nullptr;

	for (uint32_t i = 0; i < cMaxContexts - 1; i++)
	{
		gContextFreeList[i].mNext = &gContextFreeList[i + 1];
	}

	gContextFreeList[cMaxContexts - 1].mNext = nullptr;
}

Context* allocContext(Context* current, Cell* variables, Cell* params, Context* outer)
{
	if (gContextFreeList == nullptr)
	{
		gc(current);
		assert(gContextFreeList);
	}
	
	auto context		= gContextFreeList;
	gContextFreeList	= context->mNext;
	context->mNext		= gContextAllocList;
	gContextAllocList	= context;
	return new (context)Context(variables, params, outer);
}

Cell* allocCell( Context* current, Item car, Item cdr = Item((Cell*)nullptr))
{
	if (gCellFreeList == nullptr)
	{
		gc(current);
		assert(gCellFreeList);;
	}

	auto cell = gCellFreeList;
	gCellFreeList = cell->mNext;
	cell->mNext = gCellAllocList;
	gCellAllocList = cell;

	return new (cell)Cell(car, cdr);
}

void freeCell(Cell* cell)
{
	cell->mNext = gCellFreeList;
	gCellFreeList = cell;
}

void freeContext(Context* context)
{
	context->mNext = gContextFreeList;
	gContextFreeList = context;
}

void markCell(Cell* cell)
{
	if (cell->mReachable)
	{
		return;
	}

	cell->mReachable = true;
	if (cell->mCar.mTag == eCell && cell->mCar.mCell)
	{
		markCell(cell->mCar.mCell);
	}

	if (cell->mCdr.mTag == eCell && cell->mCdr.mCell )
	{
		markCell(cell->mCdr.mCell);
	}
}

void markContext(Context* context)
{
	if (context->mReachable)
	{
		return;
	}

	context->mReachable = true;

	for (auto pair : context->mBindings)
	{
		if (gVerboseGC)
		{
			printf("(%x) marking %s\n", context, gSymbolTable.GetString(pair.first).c_str());
		}
		if (pair.second.mTag == eCell && pair.second.mCell )
		{
			markCell(pair.second.mCell);
		}
		else if (pair.second.mTag == eProc)
		{
			if (!pair.second.mProc.mNative)
			{
				markCell(pair.second.mProc.mProc);
			}
			if (pair.second.mProc.mClosure)
			{
				markContext(pair.second.mProc.mClosure);
			}
		}
	}

	if (context->mOuter)
	{
		markContext(context->mOuter);
	}
}
uint32_t collectContexts()
{
	Context*i = gContextAllocList;
	Context* prev = nullptr;
	uint32_t collected = 0;
	while (i)
	{
		Context* next = i->mNext;
		if (!i->mReachable)
		{
			if (!prev)
			{
				gContextAllocList = i->mNext;
			}
			else
			{
				prev->mNext = i->mNext;
			}

			freeContext(i);
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

uint32_t collectCells()
{
	Cell*i = gCellAllocList;
	Cell* prev = nullptr;
	uint32_t collected = 0;
	while (i)
	{
		Cell* next = i->mNext;
		if (!i->mReachable)
		{
			if (!prev)
			{
				gCellAllocList = i->mNext;
			}
			else
			{
				prev->mNext = i->mNext;
			}

			freeCell(i);
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

void gc( Context* context )
{
	uint32_t count = 0;
	Cell* i = gCellAllocList;
	while (i)
	{
		i->mReachable = false;
		i = i->mNext;
		count++;
	}

	uint32_t ccount = 0;
	Context* j = gContextAllocList;
	while (j)
	{
		j->mReachable = false;
		j = j->mNext;
		ccount++;
	}

	if (gVerboseGC)
	{
		printf("considering %d cells and %d contexts during GC\n", count, ccount);
	}

	markContext(context);

	uint32_t cellcount = collectCells();
	uint32_t contextcount = collectContexts();

	if (gVerboseGC)
	{
		printf("return %d cells and %d contexts to the free lists\n",cellcount, contextcount);
	}
}


bool isSign(char c)
{
	return c == '-';
}

bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

int32_t parseDigits(char*cs, char** rest, int32_t previous)
{
	if (!isDigit(*cs))
	{
		*rest = cs;
		return previous;
	}
	else
	{
		int32_t digit = *cs - '0';
		cs++;
		return parseDigits(cs, rest, previous * 10 + digit);
	}
}

Maybe<Item> parseNumber(char* cs, char** rest)
{
	int32_t sign = 1;
	if ( isSign(*cs) )
	{
		sign = -1;
		cs++;
	}
	
	if (!isDigit(*cs))
	{
		return Maybe<Item>();
	}

	return Maybe<Item>( Item( parseDigits(cs, rest, 0) * sign));
}

bool isSymbolInitial(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '+' || c == '=' || c == '*' || c == '/' || c == '-' || c == '%';
}

bool isSymbolBody(char c)
{
	return isSymbolInitial(c) || isDigit(c) || (c == '-') || ( c=='?') || ( c=='!');
}

Maybe<Item> parseSymbol(char* cs, char** rest)
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
		return Maybe<Item>( Item(symbol) );
	}
	else
	{
		return Maybe<Item>();
	}
}

bool isNewline(char c)
{
	return (c == '\n') || (c == '\f') || (c == '\r');
}

bool isWhitespace(char c)
{
	return (c == ' ') || (c == '\t') || isNewline( c );
}

Maybe<void> parseWhitespace(char* cs, char** rest)
{
	Maybe<void> valid(true);
	while (isWhitespace(*cs))
	{
		cs++;
	}

	*rest = cs;
	return valid;
}

Maybe<void> parseSingleLineComment(char* cs, char** rest)
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

Maybe<void> parseAtmosphere(char* cs, char** rest)
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

Maybe<Item> parseList(Context* context, char* cs, char** rest);

Maybe<Item> parseForm(Context* context, char*cs, char** rest);

Maybe<Item> parseQuotedForm(Context* context, char*cs, char** rest)
{
	if (*cs != '\'')
	{
		return Maybe<Item>();
	}
	cs++;

	Maybe<Item> item;
	if ((item = parseForm(context, cs, rest)).mValid)
	{
		Cell* qcell = allocCell( context, Item("quote"), Item( allocCell( context, item.mV ) ));
		return Maybe<Item>(Item(qcell));
	}

	return Maybe<Item>();
}
Maybe<Item> parseUnquotedForm( Context* context, char*cs, char** rest)
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

Maybe<Item> parseForm(Context* context, char* cs, char** rest)
{
	Maybe<Item> item;
	if ((item = parseQuotedForm(context, cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parseUnquotedForm(context, cs, rest)).mValid )
	{
		return item;
	}
	return Maybe<Item>();
}

Maybe<Cell*> parseForms(Context* context, char* cs, char** rest)
{
	Maybe<Item> item;
	Cell* cell = nullptr;
	if ((item = parseForm(context, cs, rest)).mValid)
	{
		cell = allocCell(context, Item());
		cell->mCar = item.mV;
	}
	else
	{
		return Maybe<Cell*>();
	}

	parseAtmosphere(*rest, rest);

	Maybe<Cell*> tail;
	if ((tail = parseForms(context, *rest, rest)).mValid)
	{
		cell->mCdr = Item(tail.mV);
	}
	else
	{
		cell->mCdr = Item((Cell*)nullptr);
	}

	return Maybe<Cell*>(cell);
}

Maybe<Item> parseNil(char* cs, char** rest)
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

Maybe<Item> parseNonEmptyList(Context* context, char* cs, char**rest)
{
	Maybe<Item> item;
	if (*cs != '(')
	{
		return Maybe<Item>();
	}
	cs++;

	parseAtmosphere(cs, rest);
	Maybe<Cell*> cell;
	if ( (cell = parseForms(context, *rest, rest)).mValid )
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

Maybe<Item> parsePair( Context* context, char* cs, char** rest)
{
	if (*cs == '(')
	{
		cs++;
		parseAtmosphere(cs, rest);
		cs = *rest;
		Maybe<Item> first, second;
		if ((first = parseForm(context, *rest, rest)).mValid)
		{
			cs = *rest;
			parseAtmosphere(cs, rest);
			cs = *rest;

			if (*cs == '.')
			{
				cs++;
				parseAtmosphere(cs, rest);
				cs = *rest;

				if ((second = parseForm(context, cs, rest)).mValid)
				{
					cs = *rest;
					parseAtmosphere(cs, rest);
					cs = *rest;
					if (*cs == ')')
					{
						cs++;
						Cell* pair = allocCell(context, first.mV, second.mV);
						*rest = cs;
						return Maybe<Item>( Item(pair) );
					}
				}
			}
		}
	}

	return Maybe<Item>();
}

Maybe<Item> parseList(Context* context, char* cs, char** rest)
{
	Maybe<Item> item;
	if ((item = parseNil(cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parsePair(context, cs, rest)).mValid)
	{
		return item;
	}
	else if ((item = parseNonEmptyList(context, cs, rest)).mValid)
	{
		return item;
	}
	return Maybe<Item>();
}

uint32_t length(Cell* cell)
{
	if (cell == nullptr)
	{
		return 0;
	}
	else
	{
		return 1 + length(cell->mCdr.mCell);
	}
}

std::string print(Item item)
{
	std::stringstream sstream;

	switch (item.mTag)
	{
	case eNumber:
		sstream << item.mNumber << " ";
		break;
	case eSymbol:
		sstream << gSymbolTable.GetString(item.mSymbol) << " ";
		break;
	case eCell:
		if (item.mCell)
		{
			sstream << "( " << print(item.mCell->mCar) << ". " << print(item.mCell->mCdr) << ") ";
		}
		else
		{
			sstream << "() ";
		}
		break;
	case eProc:
		print( Item(item.mProc.mProc));
		break;
	case eUnspecified:
		sstream << "unspecified ";
		break;
	}

	return sstream.str();
}

Item car(Item pair)
{
	assert(pair.mTag == eCell && pair.mCell != nullptr);
	return pair.mCell->mCar;
}

Item cdr(Item pair)
{
	assert(pair.mTag == eCell && pair.mCell != nullptr);
	return pair.mCell->mCdr;
}

void cons(Item pair, Context* context, std::function<void(Item)> k )
{
	eval(car(pair), context, [context, pair, k](Item first){
		eval(car(cdr(pair)), context, [first, k,context](Item second){
			k(Item( allocCell(context, first, second)));
		}); });
}

void null(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [k](Item item){
		k(Item((item.mTag == eCell &&  item.mCell == nullptr) ? 1 : 0));
	});
}

void carProc(Item pair,Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [k](Item item){
		k(car(item));
	});
}

void cdrProc(Item pair,Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [k](Item item){
		k(cdr(item));
	});
}

void mul(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval( car(cdr(pair)), context, [first,k](Item second){
			k( Item( first.mNumber * second.mNumber ) );
		}); 
	});
}

void add(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item(first.mNumber + second.mNumber));
		});
	});
}

void sub(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item(first.mNumber - second.mNumber));
		});
	});
}


void div(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item(first.mNumber / second.mNumber));
		});
	});
}


void mod(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item(first.mNumber % second.mNumber));
		});
	});
}

void print(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		puts(print(first).c_str());
		putchar('\n');
		k(gUnspecified);
	});
}

void compare(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			if (first.mTag == second.mTag)
			{
				switch (first.mTag)
				{
				case eNumber:
					k(Item((first.mNumber == second.mNumber) ? 1 : 0));
				case eSymbol:
					k(Item((first.mSymbol == second.mSymbol) ? 1 : 0));
				default:
					k(Item(0));
				}
			}
			else
			{
				k(Item(0));
			}
		}); 
	});
}

static std::function<void(void)> gNext;

void yield(std::function<void(void)> k)
{
	gNext = k;
}

void mapeval(Item in, Context* context, std::function<void(Item)> k )
{
	if ( in.mCell == nullptr)
	{
		k(Item((Cell*)nullptr));
	}
	else
	{
		eval(in.mCell->mCar, context, [in, context,k](Item result){
			mapeval(in.mCell->mCdr, context, [context,result,k](Item rest){
				k(Item( allocCell(context, result, rest)));
			});
		});
	}
}

void eval(Item item, Context* context, std::function<void(Item)> k )
{
	gNext = nullptr;
	if (gTrace)
	{
		printf("eval: %s\n", print(item).c_str());
	}
	switch (item.mTag)
	{
	case eNumber:
		k(item);
		break;
	case eSymbol:
		k(context->Lookup(item.mSymbol));
		break;
	case eCell:
	{
		if (!item.mCell)
		{
			k(item);
		}
		else
		{
			uint32_t symbol = car(item).mSymbol;
			if (symbol == gSymbolTable.GetSymbol("quote"))
			{
				k(car(cdr(item)));
			}
			else if (symbol == gSymbolTable.GetSymbol("define"))
			{
				Item value, name, params;
				params = car(cdr(item));
				if (params.mTag == eSymbol)
				{
					name = car(cdr(item));
					if (length(item.mCell) == 3)
					{
						eval(car(cdr(cdr(item))), context, [name, context, k](Item value){ context->Set(name.mSymbol, value); k(value); });
					}
				}
				else if (params.mTag == eCell)
				{
					name = car(params);
					auto arglist = cdr(params);
					auto body = car(cdr(cdr(item)));

					value = Item( allocCell(context, arglist, Item( allocCell(context,body))), context);
					context->Set(name.mSymbol, value);
					k(value);
				}
			}
			else if (symbol == gSymbolTable.GetSymbol("set!"))
			{
				eval(car(cdr(cdr(item))), context, [context, item, k](Item v){ context->Set(car(cdr(item)).mSymbol, v); k(v); });
			}
			else if (symbol == gSymbolTable.GetSymbol("if"))
			{
				eval(car(cdr(item)), context, [item, context, k](Item b){
					if (b.mNumber)
					{
						eval(car(cdr(cdr(item))), context, k);
					}
					else if (length(item.mCell) > 3)
					{
						eval(car(cdr(cdr(cdr(item)))), context, k);
					}
					else
					{
						k(gUnspecified);
					}
				});
			}
			else if (symbol == gSymbolTable.GetSymbol("lambda"))
			{
				k(Item(cdr(item).mCell, context));
			}
			else
			{
				eval(car(item), context, [item, k, context](Item proc){
					if ( proc.mTag != eProc )
					{
						puts("&did-not-eval-to-proc\n");
					}
					else if (proc.mProc.mNative)
					{
						(proc.mProc.mNative)(Item(cdr(item).mCell), context, k);
					}
					else
					{
						Cell* params = car(Item(proc.mProc.mProc)).mCell;
						auto body = car(cdr(Item(proc.mProc.mProc)));
						mapeval(cdr(item), context, [context, params, proc, body, k](Item arglist){
							auto newContext = allocContext( context, params, arglist.mCell, proc.mProc.mClosure);
							yield([body, newContext, k](){ eval(body, newContext, k); });
						});
					}
				});
			}
		}
	}
	break;
		// fall through
	default:
		k(gUnspecified);
	}
}

void addNativeFns()
{
	gRootContext.Set(gSymbolTable.GetSymbol("cons"), Item(cons));
	gRootContext.Set(gSymbolTable.GetSymbol("car"), Item(carProc));
	gRootContext.Set(gSymbolTable.GetSymbol("cdr"), Item(cdrProc));
	gRootContext.Set(gSymbolTable.GetSymbol("="), Item(compare));
	gRootContext.Set(gSymbolTable.GetSymbol("null?"), Item(null));
	gRootContext.Set(gSymbolTable.GetSymbol("+"), Item(add));
	gRootContext.Set(gSymbolTable.GetSymbol("-"), Item(sub));
	gRootContext.Set(gSymbolTable.GetSymbol("*"), Item(mul));
	gRootContext.Set(gSymbolTable.GetSymbol("/"), Item(div));
	gRootContext.Set(gSymbolTable.GetSymbol("%"), Item(mod));
	gRootContext.Set(gSymbolTable.GetSymbol("print"), Item(print));
}

void tcoeval(Item form, Context* context, std::function<void(Item)> k)
{
	yield([form,context,k](){ eval(form, context, k); });
	while (gNext) {
		gNext();
	}
}

void repl()
{
	for (;;)
	{
		char buffer[1024];
		char* rest;
		printf(">>");
		gets_s(buffer, sizeof( buffer ));
		Maybe<Item> form = parseForm(&gRootContext, buffer, &rest);
		if (form.mValid)
		{
			tcoeval(form.mV, &gRootContext, [](Item item){
				auto s = print(item);
				puts(s.c_str());
				putchar('\n');
			});
		}
		else
		{
			puts("parse error\n");
		}
	}
}

void test_numbers()
{ 
	char* rest;
	assert((parseNumber("10",&rest)).mV.mNumber == 10);
	assert((parseNumber("0", &rest)).mV.mNumber == 0);
	assert((parseNumber("-0", &rest)).mV.mNumber == 0);
	assert((parseNumber("", &rest)).mValid == false);
	assert((parseNumber("100", &rest)).mV.mNumber == 100);
	assert((parseNumber("-123", &rest)).mV.mNumber == -123);
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

	assert( gSymbolTable.GetString( parseSymbol("cat", &rest).mV.mSymbol) == "cat");
	assert(*rest == '\0');

	assert( gSymbolTable.GetString( parseSymbol("this-is-a-symbol", &rest).mV.mSymbol ) == "this-is-a-symbol");
	assert( parseSymbol("0this-is-not-a-symbol", &rest).mValid == false);
	assert( gSymbolTable.GetString( parseSymbol("several symbols", &rest).mV.mSymbol ) == "several");
	assert( gSymbolTable.GetString( parseSymbol("UniCorn5", &rest).mV.mSymbol) == "UniCorn5");
}

void test_list()
{
	char* rest;
	assert(parseList(&gRootContext,"()",&rest).mValid);
	assert(parseList(&gRootContext,"(    )", &rest).mValid);
	assert(parseList(&gRootContext,"()", &rest).mV.mTag == eCell);
	assert(parseList(&gRootContext,"( cat )", &rest).mV.mTag == eCell);
	assert(parseList(&gRootContext,"( cat 100 unicorn )", &rest).mV.mTag == eCell);
	assert(parseList(&gRootContext,"( lambda (x) ( plus x 10 ) )", &rest).mV.mTag == eCell);

	assert( length( parseList(&gRootContext,"( cat 100 unicorn )", &rest).mV.mCell) == 3);
	assert( length(parseList(&gRootContext,"( Maddy loves ( horses and unicorns) )", &rest).mV.mCell) == 3);
	assert(length(parseList(&gRootContext,"( Maddy loves ; inject a comment \n( horses and unicorns) )", &rest).mV.mCell) == 3);
	assert(!parseList(&gRootContext,"( Maddy loves ", &rest).mValid);
}

void test_quote()
{
	char* rest;
	assert(parseForm(&gRootContext,"'hello-everyone", &rest).mValid);
	assert(parseForm(&gRootContext,"'(hello everyone)", &rest).mValid );
	assert(parseForm(&gRootContext,"'''hello-multiqoute", &rest).mValid );
}

void evals_to_number(char* datum, int32_t value, Context* context = &gRootContext)
{
	char* rest;
	auto item = parseForm(context, datum, &rest);
	assert(item.mValid);
	tcoeval(item.mV, context, [value](Item result) {
		assert(result.mTag == eNumber);
		assert(result.mNumber == value);
	});
}

void evals_to_symbol(char* datum, const char* symbol, Context* context = &gRootContext)
{
	char* rest;
	auto item = parseForm(context,datum, &rest);
	assert(item.mValid);
	tcoeval(item.mV, context, [symbol](Item result) {
		assert(result.mTag == eSymbol);
		assert(result.mSymbol == gSymbolTable.GetSymbol(symbol));
	});
}


void test_eval()
{
	evals_to_number( "10", 10);
	evals_to_number("(+ 10 1)", 11);
	evals_to_number("(* 10 10)", 100);
	evals_to_number("(/ 10 2)", 5);
	evals_to_number("(- 10 2)", 8);
	evals_to_number("(% 3 2)", 1);
	evals_to_symbol("(quote cat)", "cat");
	evals_to_symbol("(if 1 'true 'false)", "true");
	evals_to_symbol("(if 0 'true 'false)", "false");
	evals_to_symbol("((lambda (x) (if x 'true 'false)) 1)", "true");
	evals_to_symbol("((lambda (x) (if x 'true 'false)) 0)", "false");

	char* rest;
	auto list = parseForm(&gRootContext,"('a 'b (+ 1 2))", &rest).mV;
	mapeval(list, &gRootContext, [](Item item){ puts(print(item).c_str()); });
}

void test_context()
{
	char* rest;
	Context* context = new Context(&gRootContext);
	evals_to_number("(set! x 10)", 10, context);
	evals_to_number("x", 10, context);

	evals_to_symbol("(define x 'cat)", "cat", context);
	tcoeval(parseForm(context,"(define length (lambda (xs) (if (null? xs ) 0 (+ 1 (length (cdr xs))))))", &rest).mV, context, [](Item item){});
	evals_to_number("(length ())", 0, context);
	evals_to_number("(length '(cat))", 1, context);
	evals_to_number("(length '(cat 'dog))", 2, context);

	tcoeval(parseForm(context,"(define (hello-world) (print 'hello-world))", &rest).mV, context, [](Item){});
	tcoeval(parseForm(context,"(hello-world)", &rest).mV, context, [](Item){});

	tcoeval(parseForm(context,"(define (length2 xs) (if (null? xs ) 0 (+ 1 (length2 (cdr xs)) )))", &rest).mV, context, [](Item item){});
	evals_to_number("(length2 ())", 0, context);
	evals_to_number("(length2 '(cat))", 1, context);
	evals_to_number("(length2 '(cat 'dog))", 2, context);

	tcoeval(parseForm(context,"(define make-plus (lambda (x) (lambda (y) (+ x y)))))", &rest).mV, context, [](Item item){});
	tcoeval(parseForm(context,"(define plus10 (make-plus 10))", &rest).mV, context, [](Item item){});
	tcoeval(parseForm(context,"(define inc (make-plus 1))", &rest).mV, context, [](Item item){});
	evals_to_number("(plus10 1)", 11, context);
	evals_to_number("(inc 10)", 11, context);

	tcoeval(parseForm(context,"(define map (lambda (p xs)" 
				   "(if (null? xs) ()" 
				   "( cons (p (car xs)) (map p (cdr xs))))))", &rest).mV, context, [](Item item){});

	tcoeval(parseForm(context,"(map inc '(1 2 3))", &rest).mV, context, [](Item item){});
}


int main(int argc, char* argv[])
{
	allocFreeLists();
	addNativeFns();

	test_numbers();
	test_symbols();
	test_comments();
	test_atmosphere();
	test_list();
	test_quote();

	test_eval();
	test_context();

	repl();
}
