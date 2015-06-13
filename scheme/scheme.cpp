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
#include "maybe.h"
#include "symboltable.h"
#include "memory.h"
#include "parser.h"
#include "list.h"

bool gTrace = false;
bool gVerboseGC = false;

SymbolTable gSymbolTable;
Memory		gMemory;

struct Context;
void eval(Item item, Context* context, std::function<void(Item)> k);

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

void cons(Item pair, Context* context, std::function<void(Item)> k )
{
	eval(car(pair), context, [context, pair, k](Item first){
		eval(car(cdr(pair)), context, [first, k,context](Item second){
			k(Item( gMemory.allocCell(context, first, second)));
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

int compareShallow(Item first, Item second)
{
	if (first.type() != second.type())
	{
		return 0;
	}
	else if (first.type() == eNumber)
	{
		return compareAny<Number>(first, second);
	}
	else if (first.type() == eProc)
	{
		return compareAny<Proc>(first, second);
	}
	else if (first.type() == eSymbol)
	{
		return compareAny<Symbol>(first, second);
	}
	else
	{
		return compareAny<CellRef>(first, second);
	}

	return 0;
}

void compare(Item pair, Context* context, std::function<void(Item)> k)
{
	eval(car(pair), context, [context, k, pair](Item first) {
		eval(car(cdr(pair)), context, [first, k](Item second){	
			k(compareShallow(first, second));
		});
	});
}

bool compareDeep(Item first, Item second)
{
	if (first.type() != second.type())
	{
		return false;
	}
	else
	{
		if (first.type() == eCell)
		{
			auto cell0 = boost::any_cast<CellRef>(first);
			auto cell1 = boost::any_cast<CellRef>(second);

			if (!cell0)
			{
				return !cell1;
			}

			if (compareDeep(cell0->mCar, cell1->mCar))
			{
				return compareDeep(cell0->mCdr, cell1->mCdr);
			}
			return false;
		}
		else
		{
			return compareShallow(first, second);
		}
	}
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
				k(Item( gMemory.allocCell(context, result, rest)));
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
		auto newcontext = gMemory.allocContext( context, context);
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
	auto newContext = gMemory.allocContext(context,context);
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
		auto lst = boost::any_cast<CellRef>(item);
		if (lst->length() == 3)
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

		value = Proc( gMemory.allocCell(context, arglist, Item( gMemory.allocCell(context, body))), context);
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
				auto newContext = gMemory.allocContext(context, params, boost::any_cast<CellRef>(arglist), boost::any_cast<Proc>(proc).mClosure);
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
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("cons"), Item( Proc(cons) ));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("car"), Item( Proc(carProc) ));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("cdr"), Item( Proc( cdrProc) ));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("="), Item( Proc( compare )));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("null?"), Item( Proc(null)));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("+"), Item( Proc(add) ));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("-"), Item( Proc( sub )));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("*"), Item( Proc(mul)));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("/"), Item( Proc(bidiv)));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("%"), Item( Proc(mod)));
	gMemory.getRoot()->Set(gSymbolTable.GetSymbol("print"), Item( Proc(biprint)));
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
		Maybe<Item> form = Parser::parseForm(gMemory.getRoot(), buffer, &rest);
		if (form.mValid)
		{
			tcoeval(form.mV, gMemory.getRoot(), [](Item item){
				auto s = print(item);
				puts(s.c_str());
				putchar('\n');
			});
		}
		else
		{
			puts("parse error\n");
		}

		gMemory.gc(gMemory.getRoot() );
	}
}

void eval_same(char* datum0, char* datum1, Context* context = gMemory.getRoot())
{
	char* rest;
	auto item = Parser::parseForm(context, datum0, &rest);
	assert(item.mValid);
	tcoeval(item.mV, context, [rest,context,datum1](Item result0) {
		char* rest2;
		auto item1 = Parser::parseForm(context, datum1, &rest2);
		tcoeval(item1.mV, context, [result0](Item result1){
			auto eq = compareDeep(result0, result1);
			assert(eq);
		});
	});
}

void evals_to_number(char* datum, int32_t value, Context* context = gMemory.getRoot())
{
	char* rest;
	auto item = Parser::parseForm(context, datum, &rest);
	assert(item.mValid);
	tcoeval(item.mV, context, [value](Item result) {
		assert(result.type() == eNumber);
		assert( boost::any_cast<Number>(result) == value);
	});
}

void evals_to_symbol(char* datum, const char* symbol, Context* context = gMemory.getRoot())
{
	char* rest;
	auto item = Parser::parseForm(context,datum, &rest);
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
	auto list = Parser::parseForm(gMemory.getRoot(),"('a 'b (+ 1 2))", &rest).mV;
	mapeval(list, gMemory.getRoot(), [](Item item){ puts(print(item).c_str()); });
}

void test_context()
{
	char* rest;
	Context* context = new Context(gMemory.getRoot());
	evals_to_number("(set! x 10)", 10, context);
	evals_to_number("x", 10, context);

	evals_to_symbol("(define x 'cat)", "cat", context);
	tcoeval(Parser::parseForm(context,"(define length (lambda (xs) (if (null? xs ) 0 (+ 1 (length (cdr xs))))))", &rest).mV, context, [](Item item){});
	evals_to_number("(length ())", 0, context);
	evals_to_number("(length '(cat))", 1, context);
	evals_to_number("(length '(cat 'dog))", 2, context);

	tcoeval(Parser::parseForm(context,"(define (hello-world) (print 'hello-world))", &rest).mV, context, [](Item){});
	tcoeval(Parser::parseForm(context,"(hello-world)", &rest).mV, context, [](Item){});

	tcoeval(Parser::parseForm(context,"(define (length2 xs) (if (null? xs ) 0 (+ 1 (length2 (cdr xs)) )))", &rest).mV, context, [](Item item){});
	evals_to_number("(length2 ())", 0, context);
	evals_to_number("(length2 '(cat))", 1, context);
	evals_to_number("(length2 '(cat 'dog))", 2, context);

	tcoeval(Parser::parseForm(context,"(define make-plus (lambda (x) (lambda (y) (+ x y)))))", &rest).mV, context, [](Item item){});
	tcoeval(Parser::parseForm(context,"(define plus10 (make-plus 10))", &rest).mV, context, [](Item item){});
	tcoeval(Parser::parseForm(context,"(define inc (make-plus 1))", &rest).mV, context, [](Item item){});
	evals_to_number("(plus10 1)", 11, context);
	evals_to_number("(inc 10)", 11, context);

	tcoeval(Parser::parseForm(context,"(define map (lambda (p xs)" 
				   "(if (null? xs) ()" 
				   "( cons (p (car xs)) (map p (cdr xs))))))", &rest).mV, context, [](Item item){});

	eval_same("(map inc '(1 2 3))", "'(2 3 4)", context);

	evals_to_number("(begin "
					 "(define ( f x ) (" 
						"callcc ret ( if (= x 10) (ret x)" 
												    "( f (+ 1 x)))))" 
					  "(f 0))"
					  , 10);

	eval_same("10", "10");
	eval_same("'10", "10");
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

	addNativeFns();

	Parser::test();

	test_eval();
	test_context();

	repl();
}
