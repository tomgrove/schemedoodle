// scheme.cpp : Defines the entry point for the console application.
//
#include "stdafx.h"
#include <map>
#include <assert.h>
#include <sstream>
#include <functional>
#include "boost\any.hpp"
#include "schemetypes.h"
#include "collectable.h"
#include "context.h"

bool gTrace = false;
bool gVerboseGC = false;

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


static std::string print(Item item);

struct Context;
void eval(Item item, Context* context, std::function<void(Item)> k);
void gc(Context*);

const static uint32_t cMaxCells = 1000000;
const static uint32_t cMaxContexts = 1000;

static CellRef	   gCellFreeList;
static CellRef	   gCellAllocList;

static Context	   gRootContext;
static Context*	   gContextFreeList;
static Context*	   gContextAllocList;

template< class T>
T* makeFreeList(size_t size)
{
	T* freelist = new T[size];
	for ( uint32_t i = 0; i < size - 1; i++)
	{
		freelist[i].mNext = &freelist[i + 1];
	}
	return freelist;
}

void allocFreeLists()
{
	gCellFreeList = makeFreeList<Cell>( cMaxCells );
	gContextFreeList = makeFreeList<Context>( cMaxContexts );
	gCellAllocList = nullptr;
	gContextAllocList = &gRootContext;
}

Context* allocContext(Context* current, Context* outer)
{
	if (gContextFreeList == nullptr)
	{
		gc(current);
		assert(gContextFreeList);
	}

	auto next = gContextFreeList->mNext;
	auto context = new (gContextFreeList)Context( outer);
	gContextFreeList = next;
	context->mNext = gContextAllocList;
	gContextAllocList = context;
	return context;
}

Context* allocContext(Context* current, Item variables, Cell* params, Context* outer)
{
	if( gContextFreeList == nullptr )
	{
		gc(current);
		assert(gContextFreeList);
	}
	
	auto next = gContextFreeList->mNext;
	auto context = new (gContextFreeList)Context(variables, params, outer);
	gContextFreeList	= next;
	context->mNext		= gContextAllocList;
	gContextAllocList	= context;
	return context;
}

Cell* allocCell( Context* current, Item car, Item cdr = (CellRef)nullptr)
{
	if (gCellFreeList == nullptr)
	{
		gc(current);
		assert(gCellFreeList);;
	}

	auto next = gCellFreeList->mNext;
	auto cell = new (gCellFreeList)Cell(car, cdr );
	gCellFreeList = next;
	cell->mNext = gCellAllocList;
	gCellAllocList = cell;

	return cell;
}

void markCell(Cell* cell)
{
	if (cell->mReachable)
	{
		return;
	}

	cell->mReachable = true;
	if (cell->mCar.type() == eCell && boost::any_cast<CellRef>( cell->mCar ))
	{
		markCell( boost::any_cast<CellRef>( cell->mCar));
	}

	if (cell->mCdr.type() == eCell && boost::any_cast<CellRef>(cell->mCdr) )
	{
		markCell( boost::any_cast<CellRef>( cell->mCdr));
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
		if (pair.second.type() == eCell && boost::any_cast<CellRef>( pair.second) )
		{
			markCell( boost::any_cast<CellRef>( pair.second) );
		}
		else if (pair.second.type() == eProc)
		{
			if (!boost::any_cast<Proc>(pair.second).mNative)
			{
				markCell( boost::any_cast<Proc>(pair.second).mProc);
			}
			if ( boost::any_cast<Proc>( pair.second).mClosure)
			{
				markContext( boost::any_cast<Proc>(pair.second).mClosure);
			}
		}
	}

	if (context->mOuter)
	{
		markContext(context->mOuter);
	}
}

template<class T>
uint32_t mark(T* collectables)
{
	uint32_t count = 0;
	T* i = collectables;
	while (i)
	{
		i->mReachable = false;
		i = i->mNext;
		count++;
	}

	return count;
}

template<class T>
uint32_t collect( T** alloclist, T** freelist  )
{
	T* i = *alloclist;
	T* prev = nullptr;
	uint32_t collected = 0;
	while (i)
	{
		T* next = i->mNext;
		if (!i->mReachable)
		{
			if (!prev)
			{
				*alloclist = i->mNext;
			}
			else
			{
				prev->mNext = i->mNext;
			}

			i->mNext = *freelist;
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

void gc( Context* context )
{
	uint32_t cellcount = mark(gCellAllocList);
	uint32_t contextcount = mark(gContextAllocList);

	if (gVerboseGC)
	{
		printf("considering %d cells and %d contexts during GC\n", cellcount, contextcount);
	}

	markContext(context);

	uint32_t gc_cellcount = collect( &gCellAllocList, &gCellFreeList);
	uint32_t gc_contextcount = collect( &gContextAllocList, &gContextFreeList);

	if (gVerboseGC)
	{
		printf("return %d cells and %d contexts to the free lists\n",gc_cellcount, gc_contextcount);
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
		return Maybe<Item>(Item(gSymbolTable.GetSymbol(symbol)));
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
		Cell* qcell = allocCell( context, Item( gSymbolTable.GetSymbol("quote") ), Item( allocCell( context, item.mV ) ));
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

Maybe<Item> parsePair(Context* context, char* cs, char** rest)
{
	if (*cs != '.')
	{
		return Maybe<Item>();
	}

	cs++;
	parseAtmosphere(cs, rest);
	cs = *rest;

	Maybe<Item> item;
	if ((item = parseForm(context, cs, rest)).mValid)
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


Maybe<Item> parseList(Context* context, char* cs, char** rest)
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

uint32_t length(CellRef cell)
{
	if (cell == nullptr)
	{
		return 0;
	}
	else
	{
		return 1 + length( boost::any_cast<CellRef>(cell->mCdr));
	}
}

std::string print(Item item)
{
	std::stringstream sstream;

	if (item.type() == eNumber)
	{
		sstream << boost::any_cast<Number>(item) << " ";
	}
	else if (item.type() == eSymbol)
	{
		sstream << gSymbolTable.GetString( boost::any_cast<Symbol>(item)) << " ";
	}
	else if (item.type() == eCell)
	{
		if (boost::any_cast<CellRef>(item))
		{
			sstream << "( " << print(boost::any_cast<CellRef>(item)->mCar) << ". " << print(boost::any_cast<CellRef>(item)->mCdr) << ") ";
		}
		else
		{
			sstream << "() ";
		}
	}
	else if (item.type() == eProc)
	{
		if (boost::any_cast<Proc>(item).mProc)
		{
			sstream << "proc";
		}
		else
		{
			sstream << "native proc or continuation";
		}
	}
	else if (item.type() == eUnspecified)
	{
		sstream << "unspecified ";
	}
	
	return sstream.str();
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
		k(Item((item.type() == eCell &&  boost::any_cast<CellRef>(item) == nullptr) ? 1 : 0));
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
			k( Item( boost::any_cast<Number>(first) * boost::any_cast<Number>( second)  ) );
		}); 
	});
}

void add(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item( boost::any_cast<Number>(first) + boost::any_cast<Number>(second)));
		});
	});
}

void sub(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item( boost::any_cast<Number>(first) - boost::any_cast<Number>(second)));
		});
	});
}


void bidiv(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item( boost::any_cast<Number>(first) / boost::any_cast<Number>(second)));
		});
	});
}


void mod(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
			k(Item( boost::any_cast<Number>(first) %  boost::any_cast<Number>(second) ));
		});
	});
}

void biprint(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		puts(print(first).c_str());
		putchar('\n');
		k( Unspecified() );
	});
}

template<typename T>
Number compareAny(Item first, Item second)
{
	return (boost::any_cast<T>(first) == boost::any_cast<T>(second)) ? 1 : 0;
}

void compare(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){
		
			if (first.type() != second.type())
			{
				k(0);
			}
			else if (first.type() == eNumber)
			{
				k(compareAny<Number>(first, second));
			}
			else if (first.type() == eProc)
			{
				k(compareAny<Proc>(first, second));
			}
			else if (first.type() == eSymbol)
			{
				k(compareAny<Symbol>(first, second));
			}
			else if (first.type() == eCell)
			{
				k(compareAny<CellRef>(first, second));
			}
			else
			{
				k(0);
			}
		});
	});
}

static std::function<void(void)> gNext;

void yield(std::function<void(void)> k)
{
	gNext = k;
}

void mapeval(Item in, Context* context, std::function<void(Item)> k)
{
	if (boost::any_cast<CellRef>(in) == nullptr)
	{
		k(Item((Cell*)nullptr));
	}
	else
	{
		eval(boost::any_cast<CellRef>(in)->mCar, context, [in, context, k](Item result){
			mapeval(boost::any_cast<CellRef>(in)->mCdr, context, [context, result, k](Item rest){
				k(Item(allocCell(context, result, rest)));
			});
		});
	}
}

void eval_begin(Item body, Context* context, std::function<void(Item)> k)
{
	if (!boost::any_cast<CellRef>(cdr(body)))
	{
		eval(car(body), context, k);
	}
	else
	{
		eval(car(body), context, [body, context, k](Item){ eval_begin(cdr(body), context, k); });
	}
}

void eval_letstar_rec(Item defs, Context* context, std::function<void(Context*)> k)
{
	if (!boost::any_cast<CellRef>(defs))
	{
		k( context);
	}
	else
	{
		Item defpair = car(defs);
		Item symbol = car(defpair);
		Item def = car(cdr(defpair));
		auto newcontext = allocContext( context, context);
		eval(def, context, [newcontext, context, defs, k](Item item){
			Symbol symbol = boost::any_cast<Symbol>(car(car(defs)));
			newcontext->Set(symbol, item);
			eval_letstar_rec(cdr(defs), newcontext, k);
		});
	}
}

void eval_let_rec(Item defs, Context* evalcontext, Context* defcontext, std::function<void(Context*)> k)
{
	if (!boost::any_cast<CellRef>(defs))
	{
		k(defcontext);
	}
	else
	{
		Item defpair = car(defs);
		Item symbol = car(defpair);
		Item def = car(cdr(defpair));
		eval(def, evalcontext, [defcontext, evalcontext,defs,k](Item item){
			Symbol symbol = boost::any_cast<Symbol>(car(car(defs)));
			defcontext->Set( symbol, item);
			eval_let_rec(cdr(defs), evalcontext, defcontext, k);
		});
	}
}

void eval_let(Item item, Context* context, std::function<void(Item)> k)
{
	// (let ((x <def>)* ) <body>) 
	Symbol let = boost::any_cast<Symbol>(car(item));
	Item defs = car(cdr(item));
	Item body = car(cdr(cdr(item)));
	auto newContext = allocContext(context,context);
	if ( let== gSymbolTable.GetSymbol("let"))
	{
		eval_let_rec(defs, context, newContext, [body, k](Context* c){ eval(body, c, k); });
	}
	else if ( let == gSymbolTable.GetSymbol("let*"))
	{
		eval_letstar_rec(defs, context, [body, k](Context* c){ eval(body, c, k); });
	}
}


void eval_define(Item item, Context* context, std::function<void(Item)> k)
{
	Item value, name, params;
	params = car(cdr(item));

	// (define x ...)
	if (params.type() == eSymbol)
	{
		name = car(cdr(item));

		// (define x y )
		if (length(boost::any_cast<CellRef>(item)) == 3)
		{
			eval(car(cdr(cdr(item))), context, [name, context, k](Item value){
				context->Set(boost::any_cast<Symbol>(name), value);
				k(value);
			});
		}
		// (define x )
		else
		{
			context->Set(boost::any_cast<Symbol>(name), Unspecified());
			k(Unspecified());
		}
	}
	// (define (f ...) (body))
	else if (params.type() == eCell)
	{
		name = car(params);
		auto arglist = cdr(params);
		auto body = car(cdr(cdr(item)));

		value = Proc(allocCell(context, arglist, Item(allocCell(context, body))), context);
		context->Set(boost::any_cast<Symbol>(name), value);
		k(value);
	}
	else
	{
		puts("&invalid-define");
	}
}

void eval_if(Item item, Context* context, std::function<void(Item)> k )
{
	eval(car(cdr(item)), context, [item, context, k](Item b){
		if (boost::any_cast<Number>(b))
		{
			eval(car(cdr(cdr(item))), context, k);
		}
		else if (length(boost::any_cast<CellRef>(item)) > 3)
		{
			eval(car(cdr(cdr(cdr(item)))), context, k);
		}
		else
		{
			k(Unspecified());
		}
	});
}

void eval_proc(Item item, Context* context, std::function<void(Item)> k)
{
	eval(car(item), context, [item, k, context](Item proc){
		if (proc.type() != eProc)
		{
			puts("&did-not-eval-to-proc\n");
		}
		else if (boost::any_cast<Proc>(proc).mNative)
		{
			(boost::any_cast<Proc>(proc).mNative)(Item(boost::any_cast<CellRef>(cdr(item))), context, k);
		}
		else
		{
			auto params = car(Item(boost::any_cast<Proc>(proc).mProc));
			auto body = car(cdr(Item(boost::any_cast<Proc>(proc).mProc)));
			mapeval(cdr(item), context, [context, params, proc, body, k](Item arglist){
				auto newContext = allocContext(context, params, boost::any_cast<CellRef>(arglist), boost::any_cast<Proc>(proc).mClosure);
				yield([body, newContext, k](){ eval(body, newContext, k); });
			});
		}
	});
}

void eval(Item item, Context* context, std::function<void(Item)> k )
{
	gNext = nullptr;
	if (gTrace)
	{
		printf("eval: %s\n", print(item).c_str());
	}
	
	if (item.type() == eNumber)
	{
		k(item);
	}
	else if (item.type() == eSymbol)
	{
		k(context->Lookup( boost::any_cast<Symbol>(item)));
	}
	else if (item.type() == eCell)
	{
		if ( !boost::any_cast<CellRef>( item ) )
		{
			k(item);
		}
		else if (car(item).type() == eSymbol )
		{
			Symbol symbol = boost::any_cast<Symbol>( car(item) );
			if (symbol == gSymbolTable.GetSymbol("quote"))
			{
				k(car(cdr(item)));
			}
			else if (symbol == gSymbolTable.GetSymbol("define"))
			{
				eval_define(item, context, k);
			}
			else if (symbol == gSymbolTable.GetSymbol("set!"))
			{
				eval(car(cdr(cdr(item))), context, [context, item, k](Item v){ 
					context->Set( boost::any_cast<Symbol>(car(cdr(item))), v); k(v); 
				});
			}
			else if (symbol == gSymbolTable.GetSymbol("if"))
			{
				eval_if(item, context, k);
			}
			else if (symbol == gSymbolTable.GetSymbol("lambda"))
			{
				k( Proc( boost::any_cast<CellRef>( cdr(item)), context));
			}
			else if (symbol == gSymbolTable.GetSymbol("callcc"))
			{
				Item cc = Proc([k](Item item, Context* c, std::function<void(Item)>){
					eval(car(item), c, [k](Item e) {
						k(e);
					});
				});
				context->Set(boost::any_cast<Symbol>(car(cdr(item))), cc);
				eval(car(cdr(cdr(item))), context, [](Item item){
					auto s = print(item);
					puts(s.c_str());
					putchar(' cc\n');
				});
			}
			else if (symbol == gSymbolTable.GetSymbol("let")|| 
				     symbol == gSymbolTable.GetSymbol("let*"))
			{
				eval_let(item, context, k);
			}
			else if (symbol == gSymbolTable.GetSymbol("begin"))
			{
				eval_begin(cdr(item), context, k);
			}
			else
			{
				eval_proc(item, context, k);
			}
		}
		else
		{
			eval_proc(item, context, k);
		}
	}
	else
	{
		k(Unspecified());
	}
}

void addNativeFns()
{
	gRootContext.Set(gSymbolTable.GetSymbol("cons"), Item( Proc(cons) ));
	gRootContext.Set(gSymbolTable.GetSymbol("car"), Item( Proc(carProc) ));
	gRootContext.Set(gSymbolTable.GetSymbol("cdr"), Item( Proc( cdrProc) ));
	gRootContext.Set(gSymbolTable.GetSymbol("="), Item( Proc( compare )));
	gRootContext.Set(gSymbolTable.GetSymbol("null?"), Item( Proc(null)));
	gRootContext.Set(gSymbolTable.GetSymbol("+"), Item( Proc(add) ));
	gRootContext.Set(gSymbolTable.GetSymbol("-"), Item( Proc( sub )));
	gRootContext.Set(gSymbolTable.GetSymbol("*"), Item( Proc(mul)));
	gRootContext.Set(gSymbolTable.GetSymbol("/"), Item( Proc(bidiv)));
	gRootContext.Set(gSymbolTable.GetSymbol("%"), Item( Proc(mod)));
	gRootContext.Set(gSymbolTable.GetSymbol("print"), Item( Proc(biprint)));
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

		gc(&gRootContext);
	}
}

void test_numbers()
{ 
	char* rest;
	assert(boost::any_cast<Number>((parseNumber("10",&rest)).mV) == 10);
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

	assert( gSymbolTable.GetString( boost::any_cast<Symbol>( parseSymbol("cat", &rest).mV)) == "cat");
	assert(*rest == '\0');

	assert( gSymbolTable.GetString( boost::any_cast<Symbol>( parseSymbol("this-is-a-symbol", &rest).mV) ) == "this-is-a-symbol");
	assert( parseSymbol("0this-is-not-a-symbol", &rest).mValid == false);
	assert( gSymbolTable.GetString( boost::any_cast<Symbol>(parseSymbol("several symbols", &rest).mV) ) == "several");
	assert( gSymbolTable.GetString( boost::any_cast<Symbol>( parseSymbol("UniCorn5", &rest).mV)) == "UniCorn5");
}

void test_list()
{
	char* rest;
	assert(parseList(&gRootContext,"()",&rest).mValid);
	assert(parseList(&gRootContext,"(    )", &rest).mValid);
	assert(parseList(&gRootContext,"()", &rest).mV.type() == eCell);
	assert(parseList(&gRootContext,"( cat )", &rest).mV.type() == eCell);
	assert(parseList(&gRootContext,"( cat vs unicorn )", &rest).mV.type() == eCell);
	assert(parseList(&gRootContext, "( Imogen loves Mummy and Daddy . xs )", &rest).mV.type() == eCell);
	assert(parseList(&gRootContext, "( first . second )", &rest).mV.type() == eCell);
	assert(parseList(&gRootContext,"( lambda (x) ( plus x 10 ) )", &rest).mV.type() == eCell);

	assert(length(boost::any_cast<CellRef>( parseList(&gRootContext,"( cat 100 unicorn )", &rest).mV)) == 3);
	assert(length(boost::any_cast<CellRef>( parseList(&gRootContext, "( Maddy loves ( horses and unicorns) )", &rest).mV)) == 3);
	assert(length(boost::any_cast<CellRef>( parseList(&gRootContext,"( Maddy loves ; inject a comment \n( horses and unicorns) )", &rest).mV)) == 3);
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
		assert(result.type() == eNumber);
		assert( boost::any_cast<Number>(result) == value);
	});
}

void evals_to_symbol(char* datum, const char* symbol, Context* context = &gRootContext)
{
	char* rest;
	auto item = parseForm(context,datum, &rest);
	assert(item.mValid);
	tcoeval(item.mV, context, [symbol](Item result) {
		assert(result.type() == eSymbol);
		assert( boost::any_cast<Symbol>( result ) == gSymbolTable.GetSymbol(symbol));
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
	//evals_to_number("((lambda x x) 10 )", 10);
	evals_to_number("( let ((x 5)) x )", 5);
	evals_to_number("( let ((x 5) (y 2)) (+ x y ) )", 7);
	evals_to_number("(let* ((x 5) (y x)) (+ x y) )", 10);
	evals_to_number("( begin (set! something 10) something)", 10);
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

	evals_to_number("(begin "
					 "(define ( f x ) (" 
						"callcc ret ( if (= x 10) (ret x)" 
												    "( f (+ 1 x)))))" 
					  "(f 0))"
					  , 10);
}

void test_any()
{
	boost::any  typeless = boost::any( 10.0 );
	assert(typeless.type() == typeid(double));

	typeless = std::function<void()>();
	assert(typeless.type() == typeid(std::function<void()>));

	auto f = boost::any_cast<std::function<void()>>(typeless);
	assert(typeid(f) == typeid(std::function<void()>));
}

int main(int argc, char* argv[])
{
	test_any();

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
