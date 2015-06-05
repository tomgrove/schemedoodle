#pragma once

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
	Maybe(bool valid)
		: mValid(valid)
	{}
	bool mValid;
};