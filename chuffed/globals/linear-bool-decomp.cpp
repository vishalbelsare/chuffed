#include "chuffed/core/sat-types.h"
#include "chuffed/core/sat.h"
#include "chuffed/primitives/primitives.h"
#include "chuffed/support/misc.h"
#include "chuffed/support/sparse_set.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/bool-view.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/vars.h"

#include <cassert>

static void bool_linear_leq(vec<Lit>& terminals, vec<Lit>& xs, int k);
static Lit bool_linear_leq(SparseSet<>& elts, vec<Lit>& vs, vec<Lit>& terminals, vec<Lit>& xs,
													 int k, int vv, int cc);

static void bool_linear_leq_std(vec<Lit>& terminals, vec<Lit>& xs, int k);
static Lit bool_linear_leq_std(SparseSet<>& elts, vec<Lit>& vs, vec<Lit>& terminals, vec<Lit>& xs,
															 int k, int vv, int cc);

void bool_linear_decomp(vec<BoolView>& x, IntRelType t, int k) {
	bool polarity;
	switch (t) {
		case IRT_LT:
			k--;
		case IRT_LE:
			polarity = true;
			break;

		case IRT_GT:
			k++;
		case IRT_GE:
			polarity = false;
			k = x.size() - k;
			break;

		default:
			NOT_SUPPORTED;
			polarity = false;
	}

	vec<Lit> vs;
	for (unsigned int ii = 0; ii < x.size(); ii++) {
		vs.push(x[ii].getLit(polarity));
	}

	vec<Lit> terminals;
	for (int ii = 0; ii <= k; ii++) {
		terminals.push(lit_True);
	}

	bool_linear_leq(terminals, vs, k);

	// // Special cases
	// if (k == 0) {
	// 	for (int ii = 0; ii < x.size(); ii++) sat.enqueue(~x[ii]);
	// 	return;
	// }
	// if (k >= x.size()) return;

	// /*
	// 	// Apparently wrong.
	// 	if(k == x.size()-1)
	// 	{
	// 		// sum_i x[i] < x.size()
	// 		// <=> sum_i ~x[i] > 0
	// 		vec<Lit> cl;
	// 		for(int ii = 0; ii < x.size(); ii++)
	// 			cl.push(~x[ii]);
	// 		sat.addClause(cl);
	// 		return;
	// 	}
	// 	*/

	// vec<Lit> out;
	// sorter(k, vs, out, SRT_CARDNET, SRT_HALF);
	// assert(out.size() > k);
	// sat.enqueue(~out[k]);
}

void bool_linear_decomp(vec<BoolView>& x, IntRelType t, IntVar* kv) {
	if (t != IRT_LE) {
		NOT_SUPPORTED;
		return;
	}

	if (kv->setMinNotR(0)) {
		kv->setMin(0);
	}
	if (kv->setMaxNotR(x.size())) {
		kv->setMax(x.size());
	}
	kv->specialiseToEL();

	const int k = kv->getMax();

	vec<Lit> terminals;
	for (int ii = 0; ii <= k; ii++) {
		terminals.push(kv->getLit(ii, LR_GE));  // kv >= ii
	}

	vec<Lit> xv;
	for (unsigned int ii = 0; ii < x.size(); ii++) {
		xv.push(x[ii].getLit(true));
	}

	bool_linear_leq_std(terminals, xv, k);
	//  bool_linear_leq(terminals, xv, k);

	// vec<Lit> vs;
	// sorter(k, xv, vs, SRT_CARDNET, SRT_FULL);
	// assert(k < vs.size());
	// assert(k < terminals.size());
	// for (int ii = 1; ii <= k; ii++) {
	// 	sat.addClause(vs[ii - 1], ~terminals[ii]);
	// 	sat.addClause(~vs[ii - 1], terminals[ii]);
	// }
}

static void bool_linear_leq(vec<Lit>& terminals, vec<Lit>& xs, int k) {
	// Special cases
	if (k == 0) {
		for (unsigned int ii = 0; ii < xs.size(); ii++) {
			sat.enqueue(~xs[ii]);
		}
		return;
	}
	if (k >= static_cast<int>(xs.size())) {
		return;
	}

	if (k == static_cast<int>(xs.size()) - 1) {
		// sum_i ~xs[i] >= 1
		vec<Lit> cl;
		for (unsigned int ii = 0; ii < xs.size(); ii++) {
			cl.push(~xs[ii]);
		}
		sat.addClause(cl);
		return;
	}

	SparseSet<> elts((k + 1) * xs.size());
	vec<Lit> vs;

	// Should add stuff for nodes that are locked T.
	const Lit r = bool_linear_leq(elts, vs, terminals, xs, k, 0, 0);
	assert(r != lit_True);
	sat.enqueue(r);
}

// {elts,vs} is the cache of known nodes.
// xs the literals, s.t. \sum_{i} xs[i] <= k.

// We're currently constructing the function for sum_{i \in 0..vv} xs[i] = cc.
static Lit bool_linear_leq(SparseSet<>& elts, vec<Lit>& vs, vec<Lit>& terminals, vec<Lit>& xs,
													 int k, int vv, int cc) {
	if (cc > k) {
		return lit_False;
	}

	if (vv == static_cast<int>(xs.size())) {
		assert(cc < static_cast<int>(terminals.size()));
		return terminals[cc];
	}

	if (elts.elem(vv * (k + 1) + cc)) {
		return vs[elts.pos(vv * (k + 1) + cc)];
	}

	Lit ret;

	const Lit low = bool_linear_leq(elts, vs, terminals, xs, k, vv + 1, cc);
	const Lit high = bool_linear_leq(elts, vs, terminals, xs, k, vv + 1, cc + 1);

	if (low == lit_False) {
		assert(high == lit_False);
		ret = lit_False;
	} else if (high == lit_True) {
		assert(low == lit_True);
		ret = lit_True;
	} else {
		assert(low != high);

		// Actually going to need to introduce a node variable.
		ret = Lit(sat.newVar(), true);

		// Introduce the clauses.
		if (low != lit_True) {
			sat.addClause(low, ~ret);
		}

		vec<Lit> cl;
		cl.push(high);
		cl.push(~xs[vv]);
		cl.push(~ret);
		sat.addClause(cl);
	}

	elts.insert(vv * (k + 1) + cc);
	assert(elts.pos(vv * (k + 1) + cc) == (unsigned int)vs.size());
	vs.push(ret);

	return ret;
}

static void bool_linear_leq_std(vec<Lit>& terminals, vec<Lit>& xs, int k) {
	// Special cases
	if (k == 0) {
		for (unsigned int ii = 0; ii < xs.size(); ii++) {
			sat.enqueue(~xs[ii]);
		}
		return;
	}

	// if(k >= xs.size())
	//   return;

	// if(k == xs.size()-1)
	// {
	//   // sum_i ~xs[i] >= 1
	//   vec<Lit> cl;
	//   for(int ii = 0; ii < xs.size(); ii++)
	//     cl.push(~xs[ii]);
	//   sat.addClause(cl);
	//   return;
	// }

	SparseSet<> elts((k + 1) * xs.size());
	vec<Lit> vs;

	// Should add stuff for nodes that are locked T.
	const Lit r = bool_linear_leq_std(elts, vs, terminals, xs, k, 0, 0);
	assert(r != lit_True);
	sat.enqueue(r);
}

// We're currently constructing the function for sum_{i \in 0..vv} xs[i] = cc.
static Lit bool_linear_leq_std(SparseSet<>& elts, vec<Lit>& vs, vec<Lit>& terminals, vec<Lit>& xs,
															 int k, int vv, int cc) {
	if (cc > k) {
		return lit_False;
	}

	if (vv == static_cast<int>(xs.size())) {
		assert(cc < static_cast<int>(terminals.size()));
		return terminals[cc];
	}

	if (elts.elem(vv * (k + 1) + cc)) {
		assert(elts.pos(vv * (k + 1) + cc) < (unsigned int)vs.size());
		return vs[elts.pos(vv * (k + 1) + cc)];
	}

	Lit ret;

	const Lit low = bool_linear_leq_std(elts, vs, terminals, xs, k, vv + 1, cc);
	const Lit high = bool_linear_leq_std(elts, vs, terminals, xs, k, vv + 1, cc + 1);

	if (low == lit_False) {
		assert(high == lit_False);
		ret = lit_False;
	} else if (high == lit_True) {
		assert(low == lit_True);
		ret = lit_True;
	} else {
		assert(low != high);

		// Actually going to need to introduce a node variable.
		ret = Lit(sat.newVar(), true);

		// Introduce the clauses.
		if (low != lit_True) {
			sat.addClause(low, ~ret);
		}

		vec<Lit> cl;
		cl.push(high);
		cl.push(~xs[vv]);
		cl.push(~ret);
		sat.addClause(cl);
	}

	elts.insert(vv * (k + 1) + cc);
	assert(elts.pos(vv * (k + 1) + cc) == (unsigned int)vs.size());
	vs.push(ret);

	return ret;
}
