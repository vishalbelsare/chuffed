#include "chuffed/globals/mddglobals.h"

#include "chuffed/core/sat.h"
#include "chuffed/mdd/MDD.h"
#include "chuffed/mdd/mdd_prop.h"
#include "chuffed/mdd/opts.h"
#include "chuffed/mdd/weighted_dfa.h"
#include "chuffed/mdd/wmdd_prop.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/int-view.h"

#include <cassert>
#include <vector>

struct dfa_trans {
	int state;
	int value;
	int dest;
};

static void addMDDProp(vec<IntVar*>& x, MDDTable& tab, MDDNodeInt m, const MDDOpts& mopts);

// MDDNodeInt fd_regular(MDDTable& tab, int n, int nstates, vec< vec<int> >& transition, int q0,
// vec<int>& accepts, bool offset = true);
static MDDNodeInt mdd_table(MDDTable& mddtab, int arity, vec<int>& doms, vec<vec<int> >& entries,
														bool is_pos);

void addMDD(vec<IntVar*>& x, MDD m, const MDDOpts& mdd_opts) {
	if (m.val == m.table->ttt().val) {
		return;
	}

	addMDDProp(x, *(m.table), m.val, mdd_opts);
	/*
	if(mdd_opts.decomp == MDDOpts::D_PROP)
	{
		addMDDProp(x, *(m.table), m.val, mdd_opts);
	} else {
		mdd_decomp_dc(x, *(m.table), m.val);
	}
	*/
}

static void addMDDProp(vec<IntVar*>& x, MDDTable& tab, MDDNodeInt m, const MDDOpts& mopts) {
	vec<int> doms;
	vec<IntView<> > w;

	vec<intpair> bounds;
	for (unsigned int i = 0; i < x.size(); i++) {
		bounds.push(intpair(x[i]->getMin(), x[i]->getMax()));
		doms.push(x[i]->getMax() + 1);
		// assert( x[i]->getMin() == 0 );
	}
	//   m = tab.bound(m, bounds);
	//   m = tab.expand(0, m);

	for (unsigned int i = 0; i < x.size(); i++) {
		x[i]->specialiseToEL();
	}
	for (unsigned int i = 0; i < x.size(); i++) {
		w.push(IntView<>(x[i], 1, 0));
	}

	auto* templ = new MDDTemplate(tab, m, doms);

	new MDDProp<0>(templ, w, mopts);
}

// x: Vars | q: # states | s: alphabet size | d[state,symbol] -> state | q0: start state | f:
// accepts States range from 1..q (0 is reserved as dead)
//
void mdd_regular(vec<IntVar*>& x, int q, int /*s*/, vec<vec<int> >& d, int q0, vec<int>& f,
								 bool offset, const MDDOpts& mopts) {
	MDDTable tab(x.size());
	const MDDNodeInt m(fd_regular(tab, x.size(), q + 1, d, q0, f, offset));
	addMDDProp(x, tab, m, mopts);
}

void mdd_table(vec<IntVar*>& x, vec<vec<int> >& t, const MDDOpts& mopts) {
	vec<int> doms;

	int maxdom = 0;
	for (unsigned int i = 0; i < x.size(); i++) {
		// assert(x[i]->getMin() == 0);
		doms.push(x[i]->getMax() + 1);

		// Could also generate maxdom from tuples.
		if ((x[i]->getMax() + 1) > maxdom) {
			maxdom = x[i]->getMax() + 1;
		}
	}
	MDDTable tab(x.size());

	// Assumes a positive table.
	const MDDNodeInt m(mdd_table(tab, x.size(), doms, t, true));

	//   tab.print_mdd_tikz(m);

	addMDDProp(x, tab, m, mopts);
}

// MDD mdd_table(MDDTable& mddtab, int arity, vec<int>& doms, vec< std::vector<unsigned int> >&
// entries, bool is_pos)
MDDNodeInt mdd_table(MDDTable& mddtab, int arity, vec<int>& doms, vec<vec<int> >& entries,
										 bool is_pos) {
	assert(static_cast<int>(doms.size()) == arity);

	MDDNodeInt table = MDDFALSE;

	for (unsigned int i = 0; i < entries.size(); i++) {
		table = mddtab.mdd_or(table, mddtab.tuple(entries[i]));
	}

	if (!is_pos) {
		std::vector<unsigned int> vdoms;
		vdoms.reserve(doms.size());
		for (unsigned int i = 0; i < doms.size(); i++) {
			vdoms.push_back(doms[i]);
		}

		//      mddtab.print_mdd_tikz(table);

		table = mddtab.mdd_not(table);
	}

	return table;
}

MDDNodeInt fd_regular(MDDTable& tab, int n, int nstates, vec<vec<int> >& transition, int q0,
											vec<int>& accepts, bool offset) {
	std::vector<std::vector<MDDNodeInt> > states;
	for (int i = 0; i < nstates; i++) {
		states.emplace_back();
		states[i].push_back(MDDFALSE);
	}

	for (unsigned int i = 0; i < accepts.size(); i++) {
		states[accepts[i] - 1][0] = MDDTRUE;
	}

	// Inefficient implementation. Should fix.
	int prevlevel = 0;
	for (int j = n - 1; j >= 0; j--) {
		for (int i = 0; i < nstates - 1; i++) {
			std::vector<edgepair> cases;
			for (unsigned int k = 0; k < transition[i].size(); k++) {
				if (transition[i][k] > 0) {
					cases.emplace_back(offset ? k + 1 : k, states[transition[i][k] - 1][prevlevel]);
				}
			}
			states[i].push_back(tab.mdd_case(j, cases));
		}
		prevlevel++;
	}

	const MDDNodeInt out(states[q0 - 1][states[0].size() - 1]);

	return out;
}

// x: Vars | q: # states | s: alphabet size | d[state,symbol] -> state | q0: start state | f:
// accepts States range from 1..q (0 is reserved as dead) offset -> alphabet symbols are 1..s
//   (0..s-1 otherwise)
//
void wmdd_cost_regular(vec<IntVar*>& x, int q, int s, vec<vec<int> >& d, vec<vec<int> >& w, int q0,
											 vec<int>& f, IntVar* cost, const MDDOpts& mopts) {
	vec<WDFATrans> T;
	// Construct the weighted transitions.
	for (int qi = 0; qi < q; qi++) {
		vec<int>& d_q(d[qi]);
		vec<int>& w_q(w[qi]);

		for (int vi = 0; vi < s; vi++) {
			const WDFATrans t = {w_q[vi], d_q[vi]};
			T.push(t);
		}
	}

	EVLayerGraph g;
	const EVLayerGraph::NodeID root = wdfa_to_layergraph(g, x.size(), s, (WDFATrans*)T, q, q0, f);
	evgraph_to_wmdd(x, cost, g, root, mopts);
}
