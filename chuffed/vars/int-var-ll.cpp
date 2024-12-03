#include "chuffed/branching/branching.h"
#include "chuffed/core/engine.h"
#include "chuffed/core/options.h"
#include "chuffed/core/sat-types.h"
#include "chuffed/core/sat.h"
#include "chuffed/support/misc.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/vars.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>

extern std::map<IntVar*, std::string> intVarString;

// val -> (val-1)/2

IntVarLL::IntVarLL(const IntVar& other) : IntVar(other), ld(2), li(0), hi(1) {
	ld[0].var = 0;
	ld[0].val = min - 1;
	ld[0].prev = -1;
	ld[0].next = 1;
	ld[1].var = 1;
	ld[1].val = max;
	ld[1].prev = 0;
	ld[1].next = -1;
	// This literal becomes true when the integer variable is
	// fixed (see updateFixed).  It's not learnable, so any
	// explanation will use the reason which includes the actual
	// bounds literals.
	valLit = Lit(sat.nVars(), true);
	const int v = sat.newVar(1, ChannelInfo(var_id, 1, 0, 0));
	sat.flags[v].setDecidable(false);
	sat.flags[v].setUIPable(false);
	sat.flags[v].setLearnable(false);
	if (isFixed()) {
		sat.cEnqueue(valLit, nullptr);
	}

	varLabel = intVarString[(IntVar*)(&other)];
	std::stringstream ss;
	ss << varLabel << "=fixed";
	litString.insert(make_pair(toInt(valLit), ss.str()));
	ss.str("");
	ss << varLabel << "=notfixed";
	litString.insert(make_pair(toInt(~valLit), ss.str()));
}

DecInfo* IntVarLL::branch() {
	// Solution-based phase saving
	if (sbps_value_selection) {
		// Check if we can branch on last solution value
		if (indomain(last_solution_value)) {  // Lazy variables don't allow to use == decisions
			if (setMinNotR(last_solution_value)) {
				return new DecInfo(this, last_solution_value - 1, 2);
			}
			return new DecInfo(this, last_solution_value, 3);
		}
	}

	switch (preferred_val) {
		case PV_MIN:
			return new DecInfo(this, min, 3);
		case PV_MAX:
			return new DecInfo(this, max - 1, 2);
		case PV_SPLIT_MIN:
			return new DecInfo(this, min + (max - min - 1) / 2, 3);
		case PV_SPLIT_MAX:
			return new DecInfo(this, min + (max - min) / 2, 2);
		case PV_MEDIAN:
			CHUFFED_ERROR("Median value selection is not supported for variables with lazy literals.\n");
		default:
			NEVER;
	}
}

inline int IntVarLL::getLitNode() {
#if DEBUG_VERBOSE
	std::cerr << "IntVarLL::getLitNode\n";
#endif
	int i = -1;
	if (freelist.size() != 0) {
		i = freelist.last();
		freelist.pop();
	} else {
		i = ld.size();
		ld.push();
	}
	return i;
}

void IntVarLL::freeLazyVar(int val) {
	int ni;
	if (val < min) {
		ni = li;
		while (ld[ni].val > val) {
			ni = ld[ni].prev;
			assert(0 <= ni && ni < static_cast<int>(ld.size()));
		}
	} else if (val >= max) {
		ni = hi;
		while (ld[ni].val < val) {
			ni = ld[ni].next;
			assert(0 <= ni && ni < static_cast<int>(ld.size()));
		}
	} else {
		NEVER;
	}
	assert(ld[ni].val == val);
	ld[ld[ni].prev].next = ld[ni].next;
	ld[ld[ni].next].prev = ld[ni].prev;
	freelist.push(ni);
}

inline Lit IntVarLL::getGELit(int v) {
	if (v > max) {
		return getMaxLit();
	}
	assert(v >= min);
	int ni = li;
	const int prev = static_cast<int>(prevDomVal(v));
	if ((vals != nullptr) && (vals[v] == 0)) {
		v = static_cast<int>(nextDomVal(v));
	}
	while (ld[ni].val < prev) {
		ni = ld[ni].next;
		assert(0 <= ni && ni < static_cast<int>(ld.size()));
	}
	if (ld[ni].val == prev) {
		return Lit(ld[ni].var, true);
	}
	// overshot, create new var and insert before ni
	const int mi = getLitNode();
#if DEBUG_VERBOSE
	std::cerr << "created new literal: " << mi << ": " << varLabel << "(" << this << ") >= " << v
						<< " || " << varLabel << "(" << this << ") <= " << prev << "\n";
#endif
	ld[mi].var = sat.getLazyVar(ChannelInfo(var_id, 1, 1, prev));
	ld[mi].val = prev;
	ld[mi].next = ni;
	ld[mi].prev = ld[ni].prev;
	ld[ni].prev = mi;
	ld[ld[mi].prev].next = mi;

	std::stringstream ss;
	ss << varLabel << ">=" << v;
	litString.insert(make_pair(ld[mi].var * 2 + 1, ss.str()));
	ss.str("");
	ss << varLabel << "<=" << prev;
	litString.insert(make_pair(ld[mi].var * 2, ss.str()));

	return Lit(ld[mi].var, true);
}

inline Lit IntVarLL::getLELit(int v) {
	if (v < min) {
		return getMinLit();
	}
	return ~getGELit(v + 1);
}

Lit IntVarLL::getLit(int64_t v, LitRel t) {
	// NOTE: Previous assertion that makes little sense. We should further
	// investigate if the comparisons with min/max make sense at different
	// decision levels.
	// So far this assertion only seems to trigger with all_different (bounds)
	// assert(engine.decisionLevel() == 0);
	if (v < min) {
		return toLit(1 ^ (t & 1));  // _, _, 1, 0
	}
	if (v > max) {
		return toLit(t & 1);  // _, _, 0, 1
	}
	switch (t) {
		case LR_GE:
			return getGELit(static_cast<int>(v));
		case LR_LE:
			return getLELit(static_cast<int>(v));
		default:
			NEVER;
	}
}

// Use when you've just set [x >= v]
inline void IntVarLL::channelMin(int v, Lit p) {
	const Reason r(~p);
	int ni;
	const int prev = static_cast<int>(prevDomVal(v));
	for (ni = ld[li].next; ld[ni].val < prev; ni = ld[ni].next) {
		sat.cEnqueue(Lit(ld[ni].var, true), r);
	}
	assert(ld[ni].val == prev);
	li = ni;
}

// Use when you've just set [x <= v]
inline void IntVarLL::channelMax(int v, Lit p) {
	const Reason r(~p);
	int ni;
	assert(!vals || vals[v]);
	for (ni = ld[hi].prev; ld[ni].val > v; ni = ld[ni].prev) {
		sat.cEnqueue(Lit(ld[ni].var, false), r);
	}
	assert(ld[ni].val == v);
	hi = ni;
}

inline void IntVarLL::updateFixed() {
	if (isFixed()) {
		const Reason r(getMinLit(), getMaxLit());
		sat.cEnqueue(valLit, r);
		changes |= EVENT_F;
	}
}

bool IntVarLL::setMin(int64_t v, Reason r, bool channel) {
	assert(setMinNotR(v));
	if ((vals != nullptr) && (vals[v] == 0)) {
		v = nextDomVal(v);
	}
	const Lit p = getGELit(static_cast<int>(v));
	if (channel) {
		sat.cEnqueue(p, r);
	}
	if (v > max) {
		assert(sat.confl);
		return false;
	}
	channelMin(static_cast<int>(v), p);
	min = static_cast<int>(v);
	changes |= EVENT_C | EVENT_L;
	updateFixed();
	pushInQueue();
	return true;
}

bool IntVarLL::setMax(int64_t v, Reason r, bool channel) {
	assert(setMaxNotR(v));
	if ((vals != nullptr) && (vals[v] == 0)) {
		v = prevDomVal(v);
	}
	const Lit p = getLELit(static_cast<int>(v));
	if (channel) {
		sat.cEnqueue(p, r);
	}
	if (v < min) {
		assert(sat.confl);
		return false;
	}
	channelMax(static_cast<int>(v), p);
	max = static_cast<int>(v);
	changes |= EVENT_C | EVENT_U;
	updateFixed();
	pushInQueue();
	return true;
}

bool IntVarLL::setVal(int64_t v, Reason r, bool channel) {
	assert(setValNotR(v));
	assert(channel);
	if (setMinNotR(v)) {
		if (!setMin(v, r, channel)) {
			return false;
		}
	}
	if (setMaxNotR(v)) {
		if (!setMax(v, r, channel)) {
			return false;
		}
	}
	return true;
}

bool IntVarLL::remVal(int64_t /*v*/, Reason /*r*/, bool channel) {  // NOLINT
	assert(channel);
	if (!engine.finished_init) {
		NEVER;
	}
	return true;
}

Lit IntVarLL::createLit(int _v) {
	const int v = _v >> 2;
	const int s = 1 - _v % 2;
	int ni = 1;
	while (ld[ni].val > v) {
		ni = ld[ni].prev;
		assert(0 <= ni && ni < static_cast<int>(ld.size()));
	}
	if (ld[ni].val == v) {
		return Lit(ld[ni].var, s != 0);
	}
	// overshot, create new var and insert before ni
	const int mi = getLitNode();
	ld[mi].var = sat.getLazyVar(ChannelInfo(var_id, 1, 1, v));
	ld[mi].val = v;
	ld[mi].prev = ni;
	ld[mi].next = ld[ni].next;
	ld[ni].next = mi;
	ld[ld[mi].next].prev = mi;

	const Lit p = Lit(ld[ld[mi].next].var, true);
	const Lit q = Lit(ld[ld[mi].prev].var, false);

	//	printf("created var %d, ", ld[mi].var);

	if (sat.value(p) == l_True) {
		auto* r = (Clause*)malloc(sizeof(Clause) + 2 * sizeof(Lit));
		r->clearFlags();
		r->temp_expl = 1;
		r->sz = 2;
		(*r)[1] = ~p;
		const int l = sat.getLevel(var(p));
		sat.rtrail[l].push(r);
		sat.aEnqueue(Lit(ld[mi].var, true), r, l);
	}
	if (sat.value(q) == l_True) {
		auto* r = (Clause*)malloc(sizeof(Clause) + 2 * sizeof(Lit));
		r->clearFlags();
		r->temp_expl = 1;
		r->sz = 2;
		(*r)[1] = ~q;
		const int l = sat.getLevel(var(q));
		sat.rtrail[l].push(r);
		sat.aEnqueue(Lit(ld[mi].var, false), r, l);
	}

	return Lit(ld[mi].var, s != 0);
}
