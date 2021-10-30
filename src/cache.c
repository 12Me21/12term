#include "xft/Xft.h"
#include "common.h"

typedef struct Key {
	Char chr: 22;
	uint8_t style: 2; // normal, bold, italc, bold+italic
} Key;

typedef struct Bucket {
	struct Item* next;
	Key key;
	XftGlyphFont value;
} Bucket;

static const int cache_length = 47327; // number !
// cache is an array of bucket pointers
static Bucket* cache[cache_length] = {NULL};

static int hash(Key key) {
	return (key.chr | (int)key.style<<21) % cache_length;
}

void cache_free(void) {
	for (int i=0; i<cache_length; i++) {
		Bucket* b=cache[i];
		while (b) {
			Bucket* next = b->next;
			FREE(b);
			b = next;
		}
		cache[i] = NULL;
	}
}

XftGlyphFont cache_lookup(Char chr, uint8_t style) {
	Key key = {chr, style}; //hopefully this fills the rest of memory with 0?
	// pointer to item
	Bucket** head = &cache[hash(key)];
	// iterate over linked list to find
	Bucket* b;
	for (b=*head; b; b=b->next) {
		if (!memcmp(&key, &b->key))
			goto found;
	}
	// not found, create new item
	ALLOC(b, 1);
	b->key = key;
	//bb->value = // load;
	// insert at head of linked list
	b->next = *head;
	*head = b;
	// return found/created item
 found:
	return b->value;
}
