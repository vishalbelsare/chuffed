#include "chuffed/mdd/MDD.h"

#include "chuffed/mdd/opcache.h"
#include "chuffed/support/vec.h"

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define OPCACHE_SZ 100000
#define CACHE_SZ 180000

static MDDEdge mkedge(unsigned int val, unsigned int dest) {
	MDDEdge edge = {(int)val, dest};
	return edge;
}

MDDNode MDDTable::allocNode(int n_edges) {
	return ((MDDNodeEl*)malloc(sizeof(MDDNodeEl) + (n_edges - 1) * sizeof(MDDEdge)));
}

inline void MDDTable::deallocNode(MDDNode node)
// void MDDTable::deallocNode(MDDNode node)
{
	free(node);
}

MDDTable::MDDTable(int _nvars)
		: nvars(_nvars),
			opcache(OpCache(OPCACHE_SZ))
#ifdef SPLIT_CACHE
					cache(new NodeCache[nvars])
#endif
{
	// Initialize \ttt and \fff.
	nodes.push_back(nullptr);  // false node
	nodes.push_back(nullptr);  // true node
	status.push_back(0);
	status.push_back(0);

	intermed = allocNode(intermed_maxsz);
}

MDDTable::~MDDTable() {
	deallocNode(intermed);
	for (unsigned int i = 2; i < nodes.size(); i++) {
		deallocNode(nodes[i]);
	}

#ifdef SPLIT_CACHE
	delete[] cache;
#endif
}

// Insert a node with edges
// stack[start,...].
MDDNodeInt MDDTable::insert(unsigned int var, unsigned int low, unsigned int start, bool expand) {
#ifdef SPLIT_CACHE
	//   NodeCache& varcache( cache[node[0]] );
	NodeCache& varcache(cache[(*node)[0]]);
#else
	NodeCache& varcache(cache);
#endif

	// Ensure there's adequate space in the intermed node.
	if (intermed_maxsz < (stack.size() - start)) {
		while (intermed_maxsz < (stack.size() - start)) {
			intermed_maxsz *= 2;
		}

		deallocNode(intermed);
		intermed = allocNode(intermed_maxsz);
	}

	// Collapse joined edges and shift to the intermediate node.
	unsigned int jj = 0;
	unsigned int ii = start;

	while (ii < stack.size() && stack[ii].dest == low) {
		ii++;
	}
	if (ii < stack.size()) {
		intermed->edges[jj++] = stack[ii];
	}
	for (; ii < stack.size(); ii++) {
		if (stack[ii].dest != intermed->edges[jj - 1].dest) {
			intermed->edges[jj] = stack[ii];
			jj++;
		}
	}

	if (jj == 0 && !expand) {
		// Constant node.
		const unsigned int ret = stack[start].dest;
		stack.resize(start);
		return ret;
	}
	// Fill in the rest of intermed, and search in the cache.
	intermed->var = var;
	intermed->low = low;
	intermed->sz = jj;

	auto res = varcache.find(intermed);

	if (res != varcache.end()) {
		stack.resize(start);
		return (*res).second;
	}

	MDDNode act = allocNode(intermed->sz);

	std::memcpy(act, intermed, sizeof(MDDNodeEl) + (((int)intermed->sz) - 1) * (sizeof(MDDEdge)));

	varcache[act] = static_cast<int>(nodes.size());
	nodes.push_back(act);
	status.push_back(0);

	stack.resize(start);  // Remove the current node from the stack.
	return static_cast<int>(nodes.size() - 1);
}

template <class T>
MDDNodeInt MDDTable::tuple(vec<T>& tpl) {
	MDDNodeInt res = MDDTRUE;

	const unsigned int start = static_cast<int>(stack.size());
	for (int i = tpl.size() - 1; i >= 0; i--) {
		stack.push_back(mkedge(tpl[i], res));
		stack.push_back(mkedge(tpl[i] + 1, MDDFALSE));
		res = insert(i, MDDFALSE, start);
	}

	return res;
}

template MDDNodeInt MDDTable::tuple(vec<int>& tpl);

MDDNodeInt MDDTable::mdd_vareq(int var, int val) {
	assert(var < nvars);

	const unsigned int start = static_cast<int>(stack.size());

	stack.push_back(mkedge(val, MDDTRUE));
	stack.push_back(mkedge(val + 1, MDDFALSE));

	const MDDNodeInt res = insert(var, MDDFALSE, start);
	assert(stack.size() == start);

	return res;
}

MDDNodeInt MDDTable::mdd_varlt(int var, int val) {
	const unsigned int start = static_cast<int>(stack.size());

	stack.push_back(mkedge(val, MDDFALSE));
	const MDDNodeInt res = insert(var, MDDTRUE, start);

	assert(stack.size() == start);

	return res;
}

MDDNodeInt MDDTable::mdd_vargt(int var, int val) {
	const unsigned int start = static_cast<int>(stack.size());
	stack.push_back(mkedge(val + 1, MDDTRUE));

	const MDDNodeInt res = insert(var, MDDFALSE, start);
	assert(stack.size() == start);

	return res;
}

MDDNodeInt MDDTable::mdd_case(int var, std::vector<edgepair>& cases) {
	if (cases.empty()) {
		return MDDFALSE;
	}

	MDDNodeInt res = MDDFALSE;

	for (auto& ii : cases) {
		res = mdd_or(res, mdd_and(mdd_vareq(var, ii.first), ii.second));
	}
	return res;
}

// FIXME: Completely bogus.
MDDNodeInt MDDTable::bound(MDDNodeInt root, vec<intpair>& /*range*/) {
	return root;
	/*
	 if( root == MDDFALSE || root == MDDTRUE )
			return root;

	 assert( ((int) nodes[root]->var) < range.size() );

	 unsigned int var = nodes[root]->var;
	 int lb = range[var].first;
	 int ub = range[var].second;

	 unsigned int start = stack.size();

	 unsigned int prev = nodes[root]->low;
	 unsigned int ii = 0;
	 while( ii < nodes[root]->sz && nodes[root]->edges[ii].val < lb )
	 {
			prev = nodes[root]->edges[ii].dest;
			ii++;
	 }

	 // Eliminate node if a single edge spans the whole range.
	 if( nodes[root]->edges[ii].val > ub )
	 {
			return prev;
	 }
	 if( nodes[root]->edges[ii].val == lb &&
				(nodes[root]->sz == ii+1 || nodes[root]->edges[ii+1].val > ub) )
	 {
			return nodes[root]->edges[ii].dest;
	 }

	 if( nodes[root]->edges[ii].val > lb )
			stack.push_back( mkedge(lb, prev) );

	 for( ; ii < nodes[root]->sz && nodes[root]->edges[ii].val <= ub; ii++ )
	 {
			stack.push_back( nodes[root]->edges[ii] );
	 }
	 stack.push_back( mkedge(ub+1,MDDFALSE) );

	 MDDNodeInt res = insert(var, MDDFALSE, start);
	 return res;
	 */
}

MDDNodeInt MDDTable::expand(int var, MDDNodeInt r) {
	if (r == MDDFALSE) {
		return MDDFALSE;
	}

	MDDNodeInt res = opcache.check(OP_EXPAND, var, r);
	if (res != UINT_MAX) {
		return res;
	}

	const int cvar = (r == MDDTRUE) ? nvars : nodes[r]->var;
	assert(cvar >= var && var <= nvars);

	const int start = static_cast<int>(stack.size());
	int low;

	if (cvar == var) {
		if (r == MDDTRUE) {
			return r;
		}

		low = expand(var + 1, nodes[r]->low);
		for (unsigned int ii = 0; ii < nodes[r]->sz; ii++) {
			stack.push_back(mkedge(nodes[r]->edges[ii].val, expand(var + 1, nodes[r]->edges[ii].dest)));
		}
	} else {
		// var < cvar
		assert(var < cvar);
		low = expand(var + 1, r);
	}
	res = insert(var, low, start, true);  // Make sure it doesn't collapse the nodes.
	assert(nodes[res]->var == (unsigned int)var);

	opcache.insert(OP_EXPAND, var, r, res);

	return res;
}

MDDNodeInt MDDTable::mdd_and(MDDNodeInt a, MDDNodeInt b) {
	if (a == MDDFALSE || b == MDDFALSE) {
		return MDDFALSE;
	}
	if (a == MDDTRUE) {
		return b;
	}
	if (b == MDDTRUE) {
		return a;
	}

	MDDNodeInt res = a < b ? opcache.check(OP_AND, a, b) : opcache.check(OP_AND, b, a);
	if (res != UINT_MAX) {
		return res;
	}

	const unsigned int start = static_cast<int>(stack.size());
	unsigned int var;
	unsigned int low;
	if (nodes[a]->var < nodes[b]->var) {
		var = nodes[a]->var;
		low = mdd_and(nodes[a]->low, b);
		for (unsigned int ii = 0; ii < nodes[a]->sz; ii++) {
			stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_and(nodes[a]->edges[ii].dest, b)));
		}
	} else if (nodes[a]->var > nodes[b]->var) {
		var = nodes[b]->var;
		low = mdd_and(a, nodes[b]->low);
		for (unsigned int ii = 0; ii < nodes[b]->sz; ii++) {
			stack.push_back(mkedge(nodes[b]->edges[ii].val, mdd_and(a, nodes[b]->edges[ii].dest)));
		}
	} else {
		// nodes[a]->var == nodes[b]->var
		var = nodes[a]->var;
		low = mdd_and(nodes[a]->low, nodes[b]->low);

		MDDNodeInt aprev = nodes[a]->low;
		MDDNodeInt bprev = nodes[b]->low;

		// Say we have:
		// A: (0: a1), (2: a2), (3: a3)
		// B: (0: b1), (1: b2), (3: b3), (7: b4)
		// A /\ B will be: (0: a1 /\ b1), (1: a1 /\ b2), (2: a2 /\ b2), (3: a3 /\ b3), (7: a3 /\ b4).
		// When unequal, we want to conjoin the least one with the *previous* value from the previous
		// pair.
		unsigned int ii = 0;
		unsigned int jj = 0;
		while (ii < nodes[a]->sz && jj < nodes[b]->sz) {
			if (nodes[a]->edges[ii].val < nodes[b]->edges[jj].val) {
				aprev = nodes[a]->edges[ii].dest;
				stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_and(aprev, bprev)));
				ii++;
			} else if (nodes[a]->edges[ii].val > nodes[b]->edges[jj].val) {
				bprev = nodes[b]->edges[jj].dest;
				stack.push_back(mkedge(nodes[b]->edges[jj].val, mdd_and(aprev, bprev)));
				jj++;
			} else {
				// a_val == b_val
				aprev = nodes[a]->edges[ii].dest;
				bprev = nodes[b]->edges[jj].dest;
				stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_and(aprev, bprev)));
				ii++;
				jj++;
			}
		}
		while (ii < nodes[a]->sz) {
			aprev = nodes[a]->edges[ii].dest;
			stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_and(aprev, bprev)));
			ii++;
		}
		while (jj < nodes[b]->sz) {
			bprev = nodes[b]->edges[jj].dest;
			stack.push_back(mkedge(nodes[b]->edges[jj].val, mdd_and(aprev, bprev)));
			jj++;
		}
	}

	res = insert(var, low, start);
	if (a < b) {
		opcache.insert(OP_AND, a, b, res);
	} else {
		opcache.insert(OP_AND, b, a, res);
	}

	return res;
}

// Should abstract out to mdd_apply(op, a, b).
MDDNodeInt MDDTable::mdd_or(MDDNodeInt a, MDDNodeInt b) {
	if (a == MDDTRUE || b == MDDTRUE) {
		return MDDTRUE;
	}
	if (a == MDDFALSE) {
		return b;
	}
	if (b == MDDFALSE) {
		return a;
	}

	MDDNodeInt res = a < b ? opcache.check(OP_OR, a, b) : opcache.check(OP_OR, b, a);
	if (res != UINT_MAX) {
		return res;
	}

	const unsigned int start = static_cast<int>(stack.size());
	unsigned int var;
	unsigned int low;
	if (nodes[a]->var < nodes[b]->var) {
		var = nodes[a]->var;
		low = mdd_or(nodes[a]->low, b);
		for (unsigned int ii = 0; ii < nodes[a]->sz; ii++) {
			stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_or(nodes[a]->edges[ii].dest, b)));
		}
	} else if (nodes[a]->var > nodes[b]->var) {
		var = nodes[b]->var;
		low = mdd_or(a, nodes[b]->low);
		for (unsigned int ii = 0; ii < nodes[b]->sz; ii++) {
			stack.push_back(mkedge(nodes[b]->edges[ii].val, mdd_or(a, nodes[b]->edges[ii].dest)));
		}
	} else {
		// nodes[a]->var == nodes[b]->var
		var = nodes[a]->var;
		low = mdd_or(nodes[a]->low, nodes[b]->low);

		MDDNodeInt aprev = nodes[a]->low;
		MDDNodeInt bprev = nodes[b]->low;

		// Say we have:
		// A: (0: a1), (2: a2), (3: a3)
		// B: (0: b1), (1: b2), (3: b3), (7: b4)
		// A /\ B will be: (0: a1 /\ b1), (1: a1 /\ b2), (2: a2 /\ b2), (3: a3 /\ b3), (7: a3 /\ b4).
		// When unequal, we want to conjoin the least one with the *previous* value from the previous
		// pair.
		unsigned int ii = 0;
		unsigned int jj = 0;
		while (ii < nodes[a]->sz && jj < nodes[b]->sz) {
			if (nodes[a]->edges[ii].val < nodes[b]->edges[jj].val) {
				aprev = nodes[a]->edges[ii].dest;
				stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_or(aprev, bprev)));
				ii++;
			} else if (nodes[a]->edges[ii].val > nodes[b]->edges[jj].val) {
				bprev = nodes[b]->edges[jj].dest;
				stack.push_back(mkedge(nodes[b]->edges[jj].val, mdd_or(aprev, bprev)));
				jj++;
			} else {
				// a_val == b_val
				aprev = nodes[a]->edges[ii].dest;
				bprev = nodes[b]->edges[jj].dest;
				stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_or(aprev, bprev)));
				ii++;
				jj++;
			}
		}
		while (ii < nodes[a]->sz) {
			aprev = nodes[a]->edges[ii].dest;
			stack.push_back(mkedge(nodes[a]->edges[ii].val, mdd_or(aprev, bprev)));
			ii++;
		}
		while (jj < nodes[b]->sz) {
			bprev = nodes[b]->edges[jj].dest;
			stack.push_back(mkedge(nodes[b]->edges[jj].val, mdd_or(aprev, bprev)));
			jj++;
		}
	}

	res = insert(var, low, start);
	if (a < b) {
		opcache.insert(OP_OR, a, b, res);
	} else {
		opcache.insert(OP_OR, b, a, res);
	}

	return res;
}

MDDNodeInt MDDTable::mdd_exist(MDDNodeInt root, unsigned int var) {
	if (root == MDDTRUE || root == MDDFALSE) {
		return root;
	}

	const unsigned int r_var = nodes[root]->var;
	if (r_var > var) {
		return root;
	}

	MDDNodeInt res = opcache.check(OP_EXIST, root, var);
	if (res != UINT_MAX) {
		return res;
	}

	if (r_var == var) {
		MDDNodeInt res = MDDFALSE;
		for (unsigned ii = 0; ii < nodes[root]->sz; ii++) {
			res = mdd_or(res, nodes[root]->edges[ii].dest);
		}
		opcache.insert(OP_EXIST, root, var, res);
		return res;
	}

	// r_var < var
	const unsigned int start = static_cast<int>(stack.size());
	const unsigned int low = mdd_exist(nodes[root]->low, var);
	for (unsigned int ii = 0; ii < nodes[root]->sz; ii++) {
		stack.push_back(
				mkedge(nodes[root]->edges[ii].val, mdd_exist(nodes[root]->edges[ii].dest, var)));
	}
	res = insert(r_var, low, start);
	opcache.insert(OP_EXIST, root, var, res);
	return res;
}

MDDNodeInt MDDTable::mdd_not(MDDNodeInt root) {
	if (root == MDDTRUE) {
		return MDDFALSE;
	}
	if (root == MDDFALSE) {
		return MDDTRUE;  // Will need to handle long edges.
	}

	const unsigned int var = nodes[root]->var;
	const unsigned int start = static_cast<int>(stack.size());

	const unsigned int low = mdd_not(nodes[root]->low);

	for (unsigned int ii = 0; ii < nodes[root]->sz; ii++) {
		stack.push_back(mkedge(nodes[root]->edges[ii].val, mdd_not(nodes[root]->edges[ii].dest)));
	}
	const MDDNodeInt res = insert(var, low, start);
	return res;
}

bool MDDTable::mdd_leq(MDDNodeInt a, MDDNodeInt b) {
	if (a == MDDFALSE) {
		return true;
	}
	if (b == MDDTRUE) {
		return true;
	}

	if (a == MDDTRUE) {
		return false;  // b != MDDTRUE
	}
	if (b == MDDFALSE) {
		return false;  // a != MDDFALSE
	}

	unsigned int res = opcache.check(OP_LEQ, a, b);
	if (res != UINT_MAX) {
		return res != 0U;
	}

	assert(nodes[a]->var == nodes[b]->var);

	res = 1U;

	unsigned int ii = 0;
	unsigned int jj = 0;
	MDDNodeInt aprev = nodes[a]->low;
	MDDNodeInt bprev = nodes[b]->low;
	while (ii < nodes[a]->sz && jj < nodes[b]->sz) {
		if (!mdd_leq(aprev, bprev)) {
			res = 0U;
			goto _mdd_leq_done;
		}
		const int aval = nodes[a]->edges[ii].val;
		const int bval = nodes[b]->edges[jj].val;

		if (aval <= bval) {
			aprev = nodes[a]->edges[ii].dest;
			ii++;
		}
		if (bval <= aval) {
			bprev = nodes[b]->edges[jj].dest;
			jj++;
		}
	}
	while (ii < nodes[a]->sz) {
		if (!mdd_leq(aprev, bprev)) {
			res = 0U;
			goto _mdd_leq_done;
		}
		aprev = nodes[a]->edges[ii].dest;
		ii++;
	}
	while (jj < nodes[b]->sz) {
		if (!mdd_leq(aprev, bprev)) {
			res = 0U;
			goto _mdd_leq_done;
		}
		bprev = nodes[b]->edges[jj].dest;
		jj++;
	}
	// Last pair
	res = static_cast<unsigned int>(mdd_leq(aprev, bprev));

_mdd_leq_done:
	opcache.insert(OP_LEQ, a, b, res);
	return res != 0U;
}

void MDDTable::clear_status(MDDNodeInt r) {
	if (status[r] == 0) {
		return;
	}
	status[r] = 0;

	if (r == MDDFALSE || r == MDDTRUE) {
		return;
	}

	clear_status(nodes[r]->low);
	for (unsigned int ii = 0; ii < nodes[r]->sz; ii++) {
		clear_status(nodes[r]->edges[ii].dest);
	}
}

void MDDTable::print_nodes() {
	for (unsigned int i = 2; i < nodes.size(); i++) {
		print_node(i);
	}
	// std::cout << nodes.size() << std::endl;
}

void MDDTable::print_node(MDDNodeInt r) {
	std::cout << r << "(" << nodes[r]->var << "): ";
	std::cout << "(..," << nodes[r]->low << ")";
	for (unsigned int jj = 0; jj < nodes[r]->sz; jj++) {
		std::cout << " (" << nodes[r]->edges[jj].val << "," << nodes[r]->edges[jj].dest << ")";
	}
	std::cout << '\n';
}

void MDDTable::print_mdd(MDDNodeInt r) {
	std::vector<MDDNodeInt> queued;
	queued.push_back(r);
	status[0] = 1;
	status[1] = 1;
	status[r] = 1;
	unsigned int head = 0;

	while (head < queued.size()) {
		const MDDNodeInt n = queued[head];

		print_node(n);
		for (unsigned int jj = 0; jj < nodes[n]->sz; jj++) {
			if (status[nodes[n]->edges[jj].dest] == 0) {
				status[nodes[n]->edges[jj].dest] = 1;
				queued.push_back(nodes[n]->edges[jj].dest);
			}
		}
		head++;
	}
	for (const unsigned int i : queued) {
		status[i] = 0;
	}
	status[0] = 0;
	status[1] = 0;
}

void MDDTable::print_mdd_tikz(MDDNodeInt /*r*/) {
	assert(0);
	// std::cout << "\\documentclass{article}\n";

	// std::cout << "\\usepackage{tikz}\n";
	// std::cout << "\\usetikzlibrary{arrows,shapes}\n";
	// std::cout << "\\begin{document}\n";
	// std::cout << "\\begin{tikzpicture}\n";
	// std::cout << "\\tikzstyle{vertex}=[draw,circle,fill=black!25,minimum size=20pt,inner
	// sep=0pt]\n"; std::cout << "\\tikzstyle{smallvert}=[circle,fill=black!25,minimum size=5pt,inner
	// sep=0pt]\n"; std::cout << "\\tikzstyle{edge} = [draw,thick,->]\n"; std::cout <<
	// "\\tikzstyle{kdedge} = [draw,thick,=>,color=red]\n"; std::cout << "\\tikzstyle{kaedge} =
	// [draw,thick,=>,color=blue]\n"; std::cout << "\\tikzstyle{kbedge} =
	// [draw,thick,=>,color=pinegreen!25]\n";

	// std::vector<MDDNodeInt> queued;
	// queued.push_back(r);
	// status[0] = 1;
	// status[1] = 1;
	// status[r] = 1;
	// unsigned int head = 0;
	// std::cout << "\\foreach \\pos/\\name/\\stat in {";

	// bool first = true;

	// int off = 0;
	// unsigned int var = 0;
	// while (head < queued.size()) {
	// 	MDDNodeInt n = queued[head];

	// 	if (first) {
	// 		first = false;
	// 		std::cout << "{(0,0)/1/T}";
	// 	}
	// 	std::cout << ",";

	// 	if (var != nodes[n][1]) {
	// 		var = nodes[n][1];
	// 		off = 0;
	// 	}

	// 	std::cout << "{(" << off << "," << 1.5 * (nvars - nodes[n][1]) << ")/" << n << "/"
	// 						<< nodes[n][1] << "}";
	// 	off += 2;

	// 	for (unsigned int j = 2; j < nodes[n][0]; j += 2) {
	// 		if (status[nodes[n][j + 1]] == 0) {
	// 			status[nodes[n][j + 1]] = 1;
	// 			queued.push_back(nodes[n][j + 1]);
	// 		}
	// 	}
	// 	head++;
	// }
	// std::cout << "}\n\t\t\\node[vertex] (\\name) at \\pos {$x_{\\stat}$};\n";

	// std::cout << "\\foreach \\source/\\dest/\\label in {";

	// first = true;
	// for (unsigned int i = 0; i < queued.size(); i++) {
	// 	MDDNodeInt n = queued[i];

	// 	for (unsigned int j = 2; j < nodes[n][0]; j += 2) {
	// 		if (first) {
	// 			first = false;
	// 		} else {
	// 			std::cout << ",";
	// 		}

	// 		std::cout << "{" << n << "/" << nodes[n][j + 1] << "/" << nodes[n][j] << "}";
	// 	}
	// }
	// std::cout << "}\n\t\t\\path[edge] (\\source) -- node {$\\label$} (\\dest);\n";

	// std::cout << "\\end{tikzpicture}\n";
	// std::cout << "\\end{document}\n";
}

void MDDTable::print_dot(MDDNodeInt r) {
	// if (r < 2) return;

	// std::cout << "digraph ingraph { graph [ranksep=\"1.0 equally\"] " << std::endl;

	// std::vector<int> queued;
	// queued.push_back(r);

	// status[r] = 1;
	// int nextid = 2;
	// unsigned int head = 0;

	// for (head = 0; head < queued.size(); head++) {
	// 	int n_id = queued[head];
	// 	MDDNodeEl* node(nodes[n_id]);
	// 	printf("  { node [shape=record label=\"{<prefix>%d: x%d | {", n_id, node->var);

	// 	bool first = true;
	// 	for (unsigned int ii = 0; ii < nodes[n_id]->sz; ii++) {
	// 		if (first)
	// 			first = false;
	// 		else
	// 			printf("|");

	// 		printf("<p%d>", ii);
	// 		if (node->edges[ii].dest < 2) {
	// 			if (node->edges[ii].dest == MDDTRUE) {
	// 				printf("T");
	// 			} else {
	// 				assert(node->edges[ii].dest == MDDFALSE);
	// 				printf("F");
	// 			}
	// 		} else {
	// 			if (!status[node->edges[ii].dest]) {
	// 				status[node->edges[ii].dest] = nextid++;
	// 				queued.push_back(node->edges[ii].dest);
	// 			}
	// 			printf("%d", node->edges[ii].dest);
	// 		}
	// 	}
	// 	printf("} }\"] %d };\n", n_id);
	// }

	// for (head = 0; head < queued.size(); head++) {
	// 	int n_id = queued[head];
	// 	MDDNodeEl* node(nodes[n_id]);

	// 	if (!(node->low < 2)) {
	// 		printf("\t%d:pL -> %d;\n", n_id, node->low);
	// 	}

	// 	for (unsigned int ii = 0; ii < node->sz; ii++) {
	// 		if (!(node->edges[ii].dest < 2)) {
	// 			printf("\t%d:p%d -> %d;\n", n_id, ii, node->edges[ii].dest);
	// 		}
	// 	}
	// }
	// std::cout << "};" << std::endl;
	// for (unsigned int ii = 0; ii < queued.size(); ii++) status[queued[ii]] = 0;
}

MDD operator|(const MDD& a, const MDD& b) {
	assert(a.table == b.table);
	return {a.table, a.table->mdd_or(a.val, b.val)};
}

MDD operator&(const MDD& a, const MDD& b) {
	assert(a.table == b.table);
	return {a.table, a.table->mdd_and(a.val, b.val)};
}

MDD operator^(const MDD& a, const MDD& b) {
	assert(a.table == b.table);
	assert(0);  // NOT IMPLEMENTED

	return {a.table, MDDFALSE};
}

MDD mdd_iff(const MDD& a, const MDD& b) {
	assert(a.table == b.table);
	assert(0);  // NOT IMPLEMENTED

	return {a.table, MDDFALSE};
}

MDD operator~(const MDD& r) { return {r.table, r.table->mdd_not(r.val)}; }

bool operator<=(const MDD& a, const MDD& b) {
	assert(a.table == b.table);
	return a.table->mdd_leq(a.val, b.val);
}
