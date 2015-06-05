#pragma once

template<class T>
struct Collectable
{
	T*		mNext;
	bool	mReachable;
	Collectable()
		: mReachable(false)
		, mNext(nullptr)
	{}
};
