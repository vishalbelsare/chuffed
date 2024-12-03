#include "chuffed/mdd/opcache.h"

#include <climits>
#include <cstdint>
#include <cstdlib>

#include <thirdparty/MurmurHash3/MurmurHash3.h>

#define USE_MURMURHASH

OpCache::OpCache(unsigned int sz)
		: tablesz(sz),

			indices((unsigned int*)malloc(sizeof(unsigned int) * sz)),
			entries((cache_entry*)malloc(sizeof(cache_entry) * sz)) {
	//    collisions = 0;
}

OpCache::~OpCache() {
	free(indices);
	free(entries);
	//    std::cout << members << ", " << collisions << std::endl;
}

struct cache_sig {
	unsigned int op;
	unsigned int a;
	unsigned int b;
};

inline unsigned int OpCache::hash(char op, unsigned int a, unsigned int b) const {
#ifndef USE_MURMURHASH
	unsigned int hash = 5381;

	hash = ((hash << 5) + hash) + op;
	hash = ((hash << 5) + hash) + a;
	hash = ((hash << 5) + hash) + b;

	return (hash & 0x7FFFFFFF) % tablesz;
#else
	uint32_t ret;
	cache_sig sig = {(unsigned int)op, a, b};
	MurmurHash3_x86_32(&sig, sizeof(cache_sig), 5381, &ret);
	return ret % tablesz;
#endif
}

// Returns UINT_MAX on failure.
unsigned int OpCache::check(char op, unsigned int a, unsigned int b) {
	const unsigned int hval = hash(op, a, b);
	const unsigned int index = indices[hval];

	if (index < members && entries[index].hash == hval) {
		// Something is in the table.
		if (entries[index].op == op && entries[index].a == a && entries[index].b == b) {
			return entries[index].res;
		}
	}
	return UINT_MAX;
}

void OpCache::insert(char op, unsigned int a, unsigned int b, unsigned int res) {
	const unsigned int hval = hash(op, a, b);
	unsigned int index = indices[hval];

	if (index >= members || entries[index].hash != hval) {
		index = members;
		indices[hval] = index;
		members++;
	}
	//   else {
	//      collisions++;
	//   }

	entries[index].hash = hval;
	entries[index].op = op;
	entries[index].a = a;
	entries[index].b = b;
	entries[index].res = res;
}
