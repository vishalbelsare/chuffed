#include "chuffed/core/options.h"
#include "chuffed/core/propagator.h"
#include "chuffed/core/sat-types.h"
#include "chuffed/core/sat.h"
#include "chuffed/mip/mip.h"
#include "chuffed/primitives/primitives.h"
#include "chuffed/support/misc.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/bool-view.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/int-view.h"
#include "chuffed/vars/vars.h"

#include <cassert>
#include <cstdint>
#include <utility>

// x >= y <- r

template <int U, int V, int R = 0>
class BinGE : public Propagator {
	const IntView<U> x;
	const IntView<V> y;
	BoolView r;

public:
	BinGE(IntView<U> _x, IntView<V> _y, BoolView _r = bv_true) : x(_x), y(_y), r(std::move(_r)) {
		x.attach(this, 0, EVENT_U);
		y.attach(this, 1, EVENT_L);
		if (R != 0) {
			r.attach(this, 2, EVENT_L);
		}
	}

	void wakeup(int /*i*/, int /*c*/) override {
		if ((R == 0) || !r.isFalse()) {
			pushInQueue();
		}
	}

	bool propagate() override {
		if ((R != 0) && r.isFalse()) {
			return true;
		}

		const int64_t x_max = x.getMax();
		const int64_t y_min = y.getMin();

		// Can finesse!!
		if ((R != 0) && x_max < y_min) {
			setDom(r, setVal, 0, x.getMaxLit(), y.getMinLit());
		}

		if ((R != 0) && !r.isTrue()) {
			return true;
		}

		// Finesses x's lower bound
		if (R != 0) {
			setDom(x, setMin, y_min, y.getMinLit(), r.getValLit());
		} else {
			setDom(x, setMin, y_min, y.getMinLit());
		}
		if (R != 0) {
			setDom(y, setMax, x_max, x.getMaxLit(), r.getValLit());
		} else {
			setDom(y, setMax, x_max, x.getMaxLit());
		}

		if (x.getMin() >= y.getMax()) {
			satisfied = true;
		}

		return true;
	}

	int checkSatisfied() override {
		if (satisfied) {
			return 1;
		}
		if (r.isFalse()) {
			satisfied = true;
			return 1;
		}
		if (x.getMin() >= y.getMax()) {
			satisfied = true;
		}
		return 3;
	}
};

//-----

// y != x <- r

template <int U = 0, int V = 0, int R = 0>
class BinNE : public Propagator, public Checker {
public:
	IntView<U> x;
	IntView<V> y;
	BoolView r;

	BinNE(IntView<U> _x, IntView<V> _y, BoolView _r = bv_true) : x(_x), y(_y), r(std::move(_r)) {
		x.attach(this, 0, EVENT_F);
		y.attach(this, 1, EVENT_F);
		if (R != 0) {
			r.attach(this, 2, EVENT_L);
		}
		//		printf("BinNE: %d %d %d\n", U, V, R);
	}

	void wakeup(int /*i*/, int /*c*/) override {
		if ((R == 0) || !r.isFalse()) {
			pushInQueue();
		}
	}

	bool propagate() override {
		if ((R != 0) && r.isFalse()) {
			return true;
		}

		if (x.isFixed() && y.isFixed() && x.getVal() == y.getVal()) {
			setDom(r, setVal, 0, x.getValLit(), y.getValLit());
		}

		if ((R != 0) && !r.isTrue()) {
			return true;
		}

		if (x.isFixed()) {
			if (R != 0) {
				setDom(y, remVal, x.getVal(), x.getValLit(), r.getValLit());
			} else {
				setDom(y, remVal, x.getVal(), x.getValLit());
			}
		}
		if (y.isFixed()) {
			if (R != 0) {
				setDom(x, remVal, y.getVal(), y.getValLit(), r.getValLit());
			} else {
				setDom(x, remVal, y.getVal(), y.getValLit());
			}
		}

		return true;
	}

	bool check() override {
		if (R != 0) {
			NOT_SUPPORTED;
		}
		return (x.getShadowVal() != y.getShadowVal());
	}

	int checkSatisfied() override {
		if (satisfied) {
			return 1;
		}
		if (r.isFalse()) {
			satisfied = true;
			return 1;
		}
		if (x.getMin() > y.getMax() || x.getMax() < y.getMin()) {
			satisfied = true;
		}
		return 3;
	}
};

//-----

// NOLINTBEGIN

#define BinProp(prop, U, V)           \
	if (u == U && v == V) {             \
		if (r.isTrue())                   \
			p = new prop<U, V, 0>(x, y, r); \
		else                              \
			p = new prop<U, V, 1>(x, y, r); \
	}

// NOLINTEND

void newBinGE(IntView<> x, IntView<> y, const BoolView& r = bv_true) {
	const int u = x.getType();
	const int v = y.getType();
	Propagator* p = nullptr;

	BinProp(BinGE, 0, 0);
	BinProp(BinGE, 0, 4);
	BinProp(BinGE, 0, 1);
	BinProp(BinGE, 0, 5);
	BinProp(BinGE, 1, 0);
	BinProp(BinGE, 5, 0);

	assert(p);
}

void newBinNE(IntView<> x, IntView<> y, const BoolView& r = bv_true) {
	const int u = x.getType();
	const int v = y.getType();
	Propagator* p = nullptr;

	BinProp(BinNE, 0, 0);
	BinProp(BinNE, 0, 4);

	assert(p);
}

//-----

// x + y rel c

void bin_linear(IntVar* x, IntVar* y, IntRelType t, int c) {
	switch (t) {
		case IRT_EQ:
			bin_linear(x, y, IRT_LE, c);
			bin_linear(x, y, IRT_GE, c);
			break;
		case IRT_LE:
			// x + y <= c <=> (-x+c) >= y
			newBinGE(IntView<>(x, -1, c), IntView<>(y));
			break;
		case IRT_LT:
			bin_linear(x, y, IRT_LE, c - 1);
			break;
		case IRT_GE:
			// x + y >= c <=> x >= (-y+c)
			newBinGE(IntView<>(x), IntView<>(y, -1, c));
			break;
		case IRT_GT:
			bin_linear(x, y, IRT_GE, c + 1);
			break;
		default:
			NEVER;
	}

	vec<int> coeffs(2, 1);
	vec<IntVar*> vars;

	vars.push(x);
	vars.push(y);

	switch (t) {
		case IRT_EQ:
			break;
		case IRT_LE:
			mip->addConstraint(coeffs, vars, -1e100, c);
			break;
		case IRT_LT:
			break;
		case IRT_GE:
			mip->addConstraint(coeffs, vars, c, 1e100);
			break;
		case IRT_GT:
			break;
		default:
			NEVER;
	}
}

//-----

struct IRR {
	IntVar* x;
	IntRelType t;
	int c;
	BoolView r;
	IRR(IntVar* _x, IntRelType _t, int _c, BoolView _r) : x(_x), t(_t), c(_c), r(std::move(_r)) {}
};

vec<IRR> ircs;
vec<IRR> ihrcs;

//-----

// x rel y + c

void int_rel(IntVar* x, IntRelType t, IntVar* y, int c) {
	switch (t) {
		case IRT_EQ:
			int_rel(x, IRT_LE, y, c);
			int_rel(x, IRT_GE, y, c);
			break;
		case IRT_NE:
			newBinNE(IntView<>(x), IntView<>(y, 1, c));
			break;
		case IRT_LE:
			// a <= b <=> b >= a
			newBinGE(IntView<>(y), IntView<>(x, 1, -c));
			break;
		case IRT_LT:
			// a < b <=> b >= (a+1)
			newBinGE(IntView<>(y), IntView<>(x, 1, 1 - c));
			break;
		case IRT_GE:
			// a >= b
			newBinGE(IntView<>(x), IntView<>(y, 1, c));
			break;
		case IRT_GT:
			// a > b <=> a >= (b+1)
			newBinGE(IntView<>(x), IntView<>(y, 1, 1 + c));
			break;
		default:
			NEVER;
	}

	vec<int> coeffs;
	vec<IntVar*> vars;

	coeffs.push(1);
	coeffs.push(-1);
	vars.push(x);
	vars.push(y);

	switch (t) {
		case IRT_EQ:
		case IRT_NE:
			break;
		case IRT_LE:
			mip->addConstraint(coeffs, vars, -1e100, c);
			break;
		case IRT_LT:
			mip->addConstraint(coeffs, vars, -1e100, c - 1);
			break;
		case IRT_GE:
			mip->addConstraint(coeffs, vars, c, 1e100);
			break;
		case IRT_GT:
			mip->addConstraint(coeffs, vars, c + 1, 1e100);
			break;
		default:
			NEVER;
	}
}

//-----

// x rel c

void int_rel(IntVar* x, IntRelType t, int c) {
	switch (t) {
		case IRT_EQ:
			TL_SET(x, setVal, c);
			break;
		case IRT_NE:
			ircs.push(IRR(x, t, c, bv_true));
			break;
		case IRT_LE:
			TL_SET(x, setMax, c);
			break;
		case IRT_LT:
			TL_SET(x, setMax, c - 1);
			break;
		case IRT_GE:
			TL_SET(x, setMin, c);
			break;
		case IRT_GT:
			TL_SET(x, setMin, c + 1);
			break;
		default:
			NEVER;
	}
}

//-----

// x rel y + c <-> r

void int_rel_reif(IntVar* x, IntRelType t, IntVar* y, const BoolView& r, int c) {
	switch (t) {
		case IRT_EQ:
			newBinGE(IntView<>(x), IntView<>(y, 1, c), r);
			newBinGE(IntView<>(y), IntView<>(x, 1, -c), r);
			newBinNE(IntView<>(x), IntView<>(y, 1, c), ~r);
			break;
		case IRT_NE:
			int_rel_reif(x, IRT_EQ, y, ~r, c);
			break;
		case IRT_LE:
			// x <= y <-> r <=> y >= x <- r /\ x >= (y+1) <- !r
			newBinGE(IntView<>(y), IntView<>(x, 1, -c), r);
			newBinGE(IntView<>(x), IntView<>(y, 1, 1 + c), ~r);
			break;
		case IRT_LT:
			// x < y <-> r <=> y >= (x+1) <- r /\ x >= y <- !r
			newBinGE(IntView<>(y), IntView<>(x, 1, 1 - c), r);
			newBinGE(IntView<>(x), IntView<>(y, 1, c), ~r);
			break;
		case IRT_GE:
			int_rel_reif(x, IRT_LT, y, ~r, c);
			break;
		case IRT_GT:
			int_rel_reif(x, IRT_LE, y, ~r, c);
			break;
		default:
			NEVER;
	}
}

//-----

// x rel y + c <- r

void int_rel_half_reif(IntVar* x, IntRelType t, int c, BoolView r) {
	ihrcs.push(IRR(x, t, c, std::move(r)));
}

void int_rel_half_reif(IntVar* x, IntRelType t, IntVar* y, const BoolView& r, int c) {
	switch (t) {
		case IRT_EQ:
			newBinGE(IntView<>(x), IntView<>(y, 1, c), r);
			newBinGE(IntView<>(y), IntView<>(x, 1, -c), r);
			break;
		case IRT_NE:
			newBinNE(IntView<>(x), IntView<>(y, 1, c), r);
			break;
		case IRT_LE:
			newBinGE(IntView<>(y), IntView<>(x, 1, -c), r);
			break;
		case IRT_LT:
			newBinGE(IntView<>(y), IntView<>(x, 1, 1 - c), r);
			break;
		case IRT_GE:
			newBinGE(IntView<>(x), IntView<>(y, 1, c), r);
			break;
		case IRT_GT:
			newBinGE(IntView<>(x), IntView<>(y, 1, 1 + c), r);
			break;
		default:
			NEVER;
	}
}

//-----
// x rel c <-> r

void int_rel_reif(IntVar* x, IntRelType t, int c, BoolView r) {
	ircs.push(IRR(x, t, c, std::move(r)));
}

void int_rel_reif_real(IntVar* x, IntRelType t, int c, BoolView r) {
	if (r.isTrue() && t == IRT_NE && x->getType() == INT_VAR_EL) {
		TL_SET(x, remVal, c);
		return;
	}
	if (x->getType() == INT_VAR) {
		assert(!so.lazy);
		IntVar* v = getConstant(c);
		int_rel_reif(x, t, v, r);
		return;
	}
	const BoolView b1(x->getLit(c, LR_GE));
	const BoolView b2(x->getLit(c, LR_LE));
	switch (t) {
		case IRT_EQ:
			bool_rel(b1, BRT_AND, b2, r);
			break;
		case IRT_NE:
			bool_rel(b1, BRT_AND, b2, ~r);
			break;
		case IRT_LE:
			bool_rel(b2, BRT_EQ, r);
			break;
		case IRT_LT:
			bool_rel(b1, BRT_EQ, ~r);
			break;
		case IRT_GE:
			bool_rel(b1, BRT_EQ, r);
			break;
		case IRT_GT:
			bool_rel(b2, BRT_EQ, ~r);
			break;
		default:
			NEVER;
	}
}

void int_rel_half_reif_real(IntVar* x, IntRelType t, int c, BoolView r) {
	if (r.isFalse()) {
		return;
	}
	if (r.isTrue() && t == IRT_NE && x->getType() == INT_VAR_EL) {
		TL_SET(x, remVal, c);
		return;
	}
	if (x->getType() == INT_VAR) {
		assert(!so.lazy);
		IntVar* v = getConstant(c);
		int_rel_half_reif(x, t, v, r);
		return;
	}
	BoolView b1(x->getLit(c, LR_GE));
	BoolView b2(x->getLit(c, LR_LE));
	switch (t) {
		case IRT_EQ:
			bool_rel(b2, BRT_OR, ~r);
			bool_rel(b1, BRT_OR, ~r);
			break;
		case IRT_NE: {
			vec<Lit> ps1;
			ps1.push(~b1);
			ps1.push(~b2);
			ps1.push(~r);
			sat.addClause(ps1);
			vec<Lit> ps2;
			ps2.push(b1);
			ps2.push(b2);
			ps2.push(~r);
			sat.addClause(ps2);
		} break;
		case IRT_LE:
			bool_rel(b2, BRT_OR, ~r);
			break;
		case IRT_LT:
			bool_rel(~b1, BRT_OR, ~r);
			break;
		case IRT_GE:
			bool_rel(b1, BRT_OR, ~r);
			break;
		case IRT_GT:
			bool_rel(~b2, BRT_OR, ~r);
			break;
		default:
			NEVER;
	}
}

void process_ircs() {
	for (unsigned int i = 0; i < ircs.size(); i++) {
		int_rel_reif_real(ircs[i].x, ircs[i].t, ircs[i].c, ircs[i].r);
	}
	ircs.clear(true);
	for (unsigned int j = 0; j < ihrcs.size(); ++j) {
		int_rel_half_reif_real(ihrcs[j].x, ihrcs[j].t, ihrcs[j].c, ihrcs[j].r);
	}
	ihrcs.clear(true);
}
