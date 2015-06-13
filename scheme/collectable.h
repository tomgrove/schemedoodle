#pragma once

struct ICollectable
{
	bool					mReachable;
	void					setReachable(bool reachable) { mReachable = reachable; }
	virtual void			mark() = 0;
};

template<class T>
struct Collectable : public ICollectable
{
	T*						mNext;
	bool					mReachable;
	Collectable()
		: mReachable(false)
		, mNext(nullptr)
	{}
};

template<class T>
class Freelist
{
public:
	T*					mAllocList;
	T*					mFreeList;
	Freelist(size_t size)
		: mAllocList( nullptr )
	{
		mFreeList = new T[size];
		for (uint32_t i = 0; i < size - 1; i++)
		{
			mFreeList[i].mNext = &mFreeList[i + 1];
		}
	}

	template<typename A>
	T* alloc(A a0)
	{
		if (mFreeList != nullptr)
		{
			auto next = mFreeList->mNext;
			auto context = new (mFreeList)T(a0);
			mFreeList = next;
			context->mNext = mAllocList;
			mAllocList = context;
			return context;
		}

		return nullptr;
	}

	template<typename A, typename B>
	T* alloc( A a0, B a1  )
	{
		if (mFreeList != nullptr)
		{
			auto next = mFreeList->mNext;
			auto context = new (mFreeList)T(a0,a1);
			mFreeList = next;
			context->mNext = mAllocList;
			mAllocList = context;
			return context;
		}

		return nullptr;
	}

	template<typename A, typename B, typename C>
	T* alloc(A a0, B a1, C a2)
	{
		if (mFreeList != nullptr)
		{
			auto next = mFreeList->mNext;
			auto context = new (mFreeList)T(a0, a1,a2);
			mFreeList = next;
			context->mNext = mAllocList;
			mAllocList = context;
			return context;
		}

		return nullptr;
	}

	uint32_t collect()
	{
		auto i = mAllocList;
		T* prev = nullptr;
		uint32_t collected = 0;
		while (i)
		{
			auto next = i->mNext;
			if (!i->mReachable)
			{
				if (!prev)
				{
					mAllocList = i->mNext;
				}
				else
				{
					prev->mNext = i->mNext;
				}

				i->mNext = mFreeList;
				mFreeList = i;
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

	uint32_t markUnreachable()
	{
		uint32_t count = 0;
		auto collectable = mAllocList;
		while (collectable)
		{
			collectable->mReachable = false;
			collectable = collectable->mNext;
			count++;
		}

		return count;
	}
};
