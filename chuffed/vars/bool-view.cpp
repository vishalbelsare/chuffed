#include "chuffed/vars/bool-view.h"

#include "chuffed/branching/branching.h"
#include "chuffed/core/propagator.h"
#include "chuffed/core/sat-types.h"
#include "chuffed/core/sat.h"
#include "chuffed/support/misc.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/vars.h"

void BoolView::attach(Propagator* p, int pos, int eflags) const {
	const WatchElem we(p->prop_id, pos);
	if ((eflags & EVENT_L) != 0 || (eflags & EVENT_F) != 0) {
		sat.watches[2 * v + static_cast<int>(s)].push(we);
	}
	if ((eflags & EVENT_U) != 0 || (eflags & EVENT_F) != 0) {
		sat.watches[2 * v + (1 - static_cast<int>(s))].push(we);
	}
}

//-----
// Branching stuff

double BoolView::getScore(VarBranch vb) {
	double min = 0;
	double max = 1;
	const bool fixed = isFixed();
	if (fixed) {
		if (isTrue()) {
			min = 1;
		} else {
			max = 0;
		}
	}
	switch (vb) {
		case VAR_MIN_MIN:
			return -min;
		case VAR_MIN_MAX:
			return min;
		case VAR_MAX_MIN:
			return -max;
		case VAR_MAX_MAX:
			return max;
		case VAR_SIZE_MIN:
			return fixed ? 0 : -1;
		case VAR_SIZE_MAX:
			return fixed ? 0 : 1;
		// TODO: Number of watches is only an estimate. Lit/Var can occur in more
		// clauses, but not function as watch. Is there a better existing measure?
		case VAR_DEGREE_MIN: {
			const vec<WatchElem>& ws = sat.watches[toInt(getValLit())];
			return -static_cast<double>(ws.size());
		}
		case VAR_DEGREE_MAX: {
			const vec<WatchElem>& ws = sat.watches[toInt(getValLit())];
			return ws.size();
		}
		case VAR_ACTIVITY:
			return sat.activity[v];
		case VAR_REGRET_MIN_MAX:
			return fixed ? 0 : 1;
		default:
			NOT_SUPPORTED;
	}
}
