#pragma once

#include "schemetypes.h"
#include "maybe.h"
#include "context.h"

class Parser
{
public:
	static Maybe<Item> parseForm(Context* context, char* cs, char** rest);
	static void test();
};