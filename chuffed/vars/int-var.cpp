#include "chuffed/vars/int-var.h"

#include "chuffed/branching/branching.h"
#include "chuffed/core/engine.h"
#include "chuffed/core/propagator.h"
#include "chuffed/core/sat-types.h"
#include "chuffed/core/sat.h"
#include "chuffed/mip/mip.h"
#include "chuffed/support/misc.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/bool-view.h"
#include "chuffed/vars/vars.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

// When set, branch variable (first_fail) and value (indomain_median,
// indomain_split, indomain_reverse_split) specifications will count domain
// sizes by the number of active values rather than the bounds (max-min).
// (There is not too much penalty if INT_DOMAIN_LIST enabled in int-var.h).
#define INT_BRANCH_HOLES 0

std::map<int, IntVar*> ic_map;

extern std::map<IntVar*, std::string> intVarString;

IntVar::IntVar(int _min, int _max)
		: var_id(engine.vars.size()), min(_min), max(_max), min0(_min), max0(_max) {
	assert(min_limit <= min && min <= max && max <= max_limit);
	engine.vars.push(this);
	changes = EVENT_C | EVENT_L | EVENT_U;
	if (isFixed()) {
		changes |= EVENT_F;
	}
}

// Allocate enough memory to specialise IntVar later using the same memory block
IntVar* newIntVar(int min, int max) {
	size_t size = sizeof(IntVar);
	if (sizeof(IntVarEL) > size) {
		size = sizeof(IntVarEL);
	}
	if (sizeof(IntVarLL) > size) {
		size = sizeof(IntVarLL);
	}
	if (sizeof(IntVarSL) > size) {
		size = sizeof(IntVarSL);
	}
	void* mem = malloc(size);
	auto* var = new (mem) IntVar(min, max);
	return var;
}

IntVar* getConstant(int v) {
	auto it = ic_map.find(v);
	if (it != ic_map.end()) {
		return it->second;
	}
	IntVar* var = newIntVar(v, v);

	std::stringstream ss;
	ss << "constant_" << v;
	intVarString[var] = ss.str();

	var->specialiseToEL();
	ic_map.emplace(v, var);

	return var;
}

void IntVar::specialiseToEL() {
	switch (getType()) {
		case INT_VAR_EL:
		case INT_VAR_SL:
			return;
		case INT_VAR:
			new (this) IntVarEL(*((IntVar*)this));
			break;
		default:
			NEVER;
	}
}

void IntVar::specialiseToLL() {
	switch (getType()) {
		case INT_VAR_EL:
		case INT_VAR_SL:
			return;
		case INT_VAR:
			new (this) IntVarLL(*((IntVar*)this));
			break;
		default:
			NEVER;
	}
}

void IntVar::specialiseToSL(vec<int>& values) {
	if (getType() == INT_VAR_EL) {
		return;
	}
	if (getType() == INT_VAR_SL) {
		return;
	}
	assert(getType() == INT_VAR);

	vec<int> v = values;
	std::sort((int*)v, (int*)v + v.size());
	unsigned int i;
	unsigned int j;
	for (i = j = 0; i < v.size(); i++) {
		if (i == 0 || v[i] != v[i - 1]) {
			v[j++] = v[i];
		}
	}
	v.resize(j);

	if (min < v[0]) {
		min = v[0];
	}
	if (max > v[v.size() - 1]) {
		max = v[v.size() - 1];
	}

	// determine whether it is sparse or dense
	if (v.last() - v[0] >= static_cast<int>(v.size() * mylog2(v.size()))) {
		// fprintf(stderr, "SL\n");
		new (this) IntVarSL(*((IntVar*)this), v);
	} else {
		new (this) IntVarEL(*((IntVar*)this));
		if (!allowSet(v)) {
			TL_FAIL();
		}
	}
}

void IntVar::initVals(bool optional) {
	if (vals != nullptr) {
		return;
	}
	if (min == min_limit || max == max_limit) {
		if (optional) {
			return;
		}
		CHUFFED_ERROR("Cannot initialise vals in unbounded IntVar\n");
	}
	vals = (Tchar*)malloc((max - min + 2) * sizeof(Tchar));
	if (vals == nullptr) {
		perror("malloc()");
		exit(1);
	}
	memset((char*)vals, 1, max - min + 2);
	vals -= min;
	if (vals == nullptr) {
		vals++;  // Hack to make vals != NULL whenever it's allocated
	}
#if INT_DOMAIN_LIST
	vals_list = (Tint*)malloc(2 * (max - min) * sizeof(Tint));
	if (!vals_list) {
		perror("malloc()");
		exit(1);
	}
	vals_list -= 2 * min + 1;
	for (int i = min; i < max; ++i) {
		vals_list[2 * i + 1].v = i + 1;  // forward link
		vals_list[2 * i + 2].v = i;      // backward link from next value
	}
	vals_count.v = max + 1 - min;
#endif
}

void IntVar::attach(Propagator* p, int pos, int eflags) {
	if (isFixed()) {
		p->wakeup(pos, eflags);
	} else {
		pinfo.push(PropInfo(p, pos, eflags));
	}
}

void IntVar::wakePropagators() {
	for (int i = pinfo.size(); (i--) != 0;) {
		PropInfo& pi = pinfo[i];
		if ((pi.eflags & changes) == 0) {
			continue;
		}
		if (pi.p->satisfied != 0) {
			continue;
		}
		if (pi.p == engine.last_prop) {
			continue;
		}
		pi.p->wakeup(pi.pos, changes);
	}
	clearPropState();
}

int IntVar::simplifyWatches() {
	unsigned int i;
	unsigned int j;
	for (i = j = 0; i < pinfo.size(); i++) {
		if (pinfo[i].p->satisfied == 0) {
			pinfo[j++] = pinfo[i];
		}
	}
	pinfo.resize(j);
	return j;
}

//-----
// Branching stuff

double IntVar::getScore(VarBranch vb) {
	switch (vb) {
		case VAR_MIN_MIN:
			return -min;
		case VAR_MIN_MAX:
			return min;
		case VAR_MAX_MIN:
			return -max;
		case VAR_MAX_MAX:
			return max;
#if INT_BRANCH_HOLES
		// note slight inconsistency, if INT_BRANCH_HOLES=0 then we
		// use the domain size-1, same behaviour but more efficient?
		case VAR_SIZE_MIN:
			return vals ? -size() : min - (max + 1);
		case VAR_SIZE_MAX:
			return vals ? size() : max + 1 - min;
#else
		case VAR_SIZE_MIN:
			return min - max;
		case VAR_SIZE_MAX:
			return max - min;
#endif
		case VAR_DEGREE_MIN:
			return -static_cast<double>(pinfo.size());
		case VAR_DEGREE_MAX:
			return pinfo.size();
		case VAR_REDUCED_COST:
			return mip->getRC(this);
		case VAR_ACTIVITY:
			return activity;
		case VAR_REGRET_MIN_MAX:
			return isFixed() ? 0 : (vals != nullptr ? *++begin() - *begin() : 1);
#ifdef HAS_VAR_IMPACT
		case VAR_IMPACT:
			return isFixed() ? 0 : impact;
#endif

		default:
			NOT_SUPPORTED;
	}
}

DecInfo* IntVar::branch() {
	//	vec<int> possible;
	// std::uniform_int_distribution<int> rnd_pos(0, possible.size() - 1);
	//	for (int i = min; i <= max; i++) if (indomain(i)) possible.push(i);
	//	return new DecInfo(this, possible[rnd_pos(engine.rnd)], 1);

	// Solution-based phase saving
	if (sbps_value_selection) {
		// Check if we can branch on last solution value
		if (indomain(last_solution_value)) {
			return new DecInfo(this, last_solution_value, 1);
		}
	}

	switch (preferred_val) {
		case PV_MIN:
			return new DecInfo(this, min, 1);
		case PV_MAX:
			return new DecInfo(this, max, 1);
#if INT_BRANCH_HOLES
		// note slight inconsistency, if INT_BRANCH_HOLES=0 then we
		// round down rather than up (vice versa for PV_SPLIT_MAX),
		// should probably revisit this and make them consistent
		case PV_SPLIT_MIN: {
			if (!vals) return new DecInfo(this, min + (max - min) / 2, 3);
			int values = (size() - 1) / 2;
			iterator j = begin();
			for (int i = 0; i < values; ++i) ++j;
			return new DecInfo(this, *j, 3);
		}
		case PV_SPLIT_MAX: {
			if (!vals) return new DecInfo(this, min + (max - min - 1) / 2, 2);
			int values = size() / 2;
			iterator j = begin();
			for (int i = 0; i < values; ++i) ++j;
			return new DecInfo(this, *j, 2);
		}
		case PV_MEDIAN: {
			if (!vals) return new DecInfo(this, min + (max - min) / 2, 1);
			int values = (size() - 1) / 2;
			iterator j = begin();
			for (int i = 0; i < values; ++i) ++j;
			return new DecInfo(this, *j, 1);
		}
#else
		case PV_SPLIT_MIN:
			return new DecInfo(this, min + (max - min - 1) / 2, 3);
		case PV_SPLIT_MAX:
			return new DecInfo(this, min + (max - min) / 2, 2);
		case PV_MEDIAN:
			if (vals == nullptr) {
				CHUFFED_ERROR("Median value selection is not supported this variable.\n");
			} else {
				const int values = (size() - 1) / 2;
				iterator j = begin();
				for (int i = 0; i < values; ++i) {
					++j;
				}
				return new DecInfo(this, *j, 1);
			}
#endif
		default:
			NEVER;
	}
}

//-----
// Domain change stuff

#if !INT_DOMAIN_LIST
inline void IntVar::updateMin() {
	int v = min;
	if (vals[v] == 0) {
		while (vals[v] == 0) {
			v++;
		}
		min = v;
		changes |= EVENT_C | EVENT_L;
	}
}

inline void IntVar::updateMax() {
	int v = max;
	if (vals[v] == 0) {
		while (vals[v] == 0) {
			v--;
		}
		max = v;
		changes |= EVENT_C | EVENT_U;
	}
}
#endif

inline void IntVar::updateFixed() {
	if (isFixed()) {
		changes |= EVENT_F;
	}
}

bool IntVar::setMin(int64_t v, Reason /*r*/, bool /*channel*/) {
	assert(setMinNotR(v));
	if (v > max) {
		return false;
	}
#if INT_DOMAIN_LIST
	if (vals) {
		int i;
		int j = vals_count;
		for (i = min; i < v; i = vals_list[2 * i + 1]) --j;
		min = i;
		vals_count = j;
	} else
		min = v;
	changes |= EVENT_C | EVENT_L;
#else
	min = static_cast<int>(v);
	changes |= EVENT_C | EVENT_L;
	if (vals != nullptr) {
		updateMin();
	}
#endif
	updateFixed();
	pushInQueue();
	return true;
}

bool IntVar::setMax(int64_t v, Reason /*r*/, bool /*channel*/) {
	assert(setMaxNotR(v));
	if (v < min) {
		return false;
	}
#if INT_DOMAIN_LIST
	if (vals) {
		int i;
		int j = vals_count;
		for (i = max; i > v; i = vals_list[2 * i]) --j;
		max = i;
		vals_count = j;
	} else
		max = v;
	changes |= EVENT_C | EVENT_U;
#else
	max = static_cast<int>(v);
	changes |= EVENT_C | EVENT_U;
	if (vals != nullptr) {
		updateMax();
	}
#endif
	updateFixed();
	pushInQueue();
	return true;
}

bool IntVar::setVal(int64_t v, Reason /*r*/, bool /*channel*/) {
	assert(setValNotR(v));
	if (!indomain(v)) {
		return false;
	}
	if (min < v) {
		min = static_cast<int>(v);
		changes |= EVENT_C | EVENT_L | EVENT_F;
	}
	if (max > v) {
		max = static_cast<int>(v);
		changes |= EVENT_C | EVENT_U | EVENT_F;
	}
#if INT_DOMAIN_LIST
	if (vals) vals_count = 1;
#endif
	pushInQueue();
	return true;
}

bool IntVar::remVal(int64_t v, Reason /*r*/, bool /*channel*/) {
	assert(remValNotR(v));
	if (isFixed()) {
		return false;
	}
	if (vals == nullptr) {
		if (!engine.finished_init) {
			NEVER;
		}
		return true;
	}
#if INT_DOMAIN_LIST
	if (v == min) {
		min = vals_list[2 * min + 1];
		changes |= EVENT_C | EVENT_L;
	} else if (v == max) {
		max = vals_list[2 * max];
		changes |= EVENT_C | EVENT_U;
	} else {
		vals[v] = 0;
		vals_list[vals_list[2 * v] * 2 + 1] = vals_list[2 * v + 1];
		vals_list[vals_list[2 * v + 1] * 2] = vals_list[2 * v];
		changes |= EVENT_C;
	}
	--vals_count;
#else
	vals[v] = 0;
	changes |= EVENT_C;
	updateMin();
	updateMax();
#endif
	updateFixed();
	pushInQueue();
	return true;
}

// Assumes v is sorted
bool IntVar::allowSet(vec<int>& v, Reason r, bool channel) {
	initVals();
	if ((vals == nullptr) && !engine.finished_init) {
		NOT_SUPPORTED;
	}
	unsigned int i = 0;
	int m = min;
	while (i < v.size() && v[i] < m) {
		i++;
	}
	for (; i < v.size(); i++) {
		for (; m < v[i]; m++) {
			if (m > max) {
				return true;
			}
			if (remValNotR(m)) {
				if (!remVal(m, r, channel)) {
					return false;
				}
			}
		}
		m = v[i] + 1;
	}
	for (; m <= max; m++) {
		if (remValNotR(m)) {
			if (!remVal(m, r, channel)) {
				return false;
			}
		}
	}
	return true;
}
