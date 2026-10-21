#ifndef CTR_NATIVE_NAMESPACE_LIST_H
#define CTR_NATIVE_NAMESPACE_LIST_H

struct Item
{
	// 0x0
	struct Item * P32 next;

	// 0x4
	struct Item * P32 prev;
};

struct LinkedList
{
	// 0x0
	struct Item * P32 first;

	// 0x4
	struct Item * P32 last;

	// 0x8
	s32 count;
};

CTR_STATIC_ASSERT(OFFSETOF(struct Item, next) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct Item, prev) == 0x4);
CTR_STATIC_ASSERT(sizeof(struct Item) == 0x8);
CTR_STATIC_ASSERT(OFFSETOF(struct LinkedList, first) == 0x0);
CTR_STATIC_ASSERT(OFFSETOF(struct LinkedList, last) == 0x4);
CTR_STATIC_ASSERT(OFFSETOF(struct LinkedList, count) == 0x8);
CTR_STATIC_ASSERT(sizeof(struct LinkedList) == 0xC);

#endif
