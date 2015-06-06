#pragma once

struct ICollectable
{
	bool					mReachable;
	virtual ICollectable*	getNext() = 0;
	virtual void			setNext(ICollectable* next) = 0;
	void					setReachable(bool reachable) { mReachable = reachable; }
	virtual void			mark() = 0;
};

template<class T>
struct Collectable : public ICollectable
{
	T*		mNext;
	bool				mReachable;
	Collectable()
		: mReachable(false)
		, mNext(nullptr)
	{}

	ICollectable* getNext() override { return mNext; }
	void		  setNext(ICollectable* next) { mNext = (T*)next; }
};
