#include "chuffed/core/engine.h"
#include "chuffed/core/options.h"
#include "chuffed/core/propagator.h"
#include "chuffed/core/sat-types.h"
#include "chuffed/core/sat.h"
#include "chuffed/ldsb/ldsb.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/int-view.h"
#include "chuffed/vars/vars.h"

class BoolArgMax : public Propagator {
public:
	const int sz;
	BoolView* const x;
	const IntView<> y;
	int offset;
	BoolArgMax(vec<BoolView> _x, int _offset, IntView<> _y)
			: sz(_x.size()), x(_x.release()), y(_y), offset(_offset) {
		priority = 1;
		for (int i = 0; i < sz; i++) {
			x[i].attach(this, i, EVENT_LU);
		}
		y.attach(this, sz, EVENT_C);
	}

	bool propagate() override {
		// l = index of first x that can be true
		// y >= l because not x[i] forall i<l
		// u = index of first x that must be true
		// y <= u because x[u]

		const int ol = static_cast<int>(y.getMin());
		for (int i = 0; i < ol - offset; i++) {
			if (x[i].setValNotR(false)) {
				Clause* r = nullptr;
				if (so.lazy) {
					r = Reason_new(2);
					(*r)[1] = y.getFMinLit(ol);
				}
				if (!x[i].setVal(false, r)) {
					return false;
				}
			}
		}

		if (y.isFixed()) {
			const int yl = static_cast<int>(y.getVal()) - offset;
			if (x[yl].setValNotR(true)) {
				Clause* r = nullptr;
				if (so.lazy) {
					r = Reason_new(2);
					(*r)[1] = y.getValLit();
				}
				if (!x[yl].setVal(true, r)) {
					return false;
				}
			}
		}

		int l = sz;
		int u = 0;

		vec<int> toFix;
		for (typename IntView<>::iterator yi = y.begin(); yi != y.end(); ++yi) {
			const int i = *yi - offset;
			if (l == sz && (!x[i].isFixed() || x[i].isTrue())) {
				l = i;
			}
			if (x[i].isFixed() && x[i].isFalse()) {
				if (y.remValNotR(i + offset)) {
					toFix.push(i);
				}
			}
			u = i;
			if (x[i].isFixed() && x[i].isTrue()) {
				break;
			}
		}
		for (unsigned int i = 0; i < toFix.size(); i++) {
			Clause* r = nullptr;
			if (so.lazy) {
				r = Reason_new(2);
				(*r)[1] = x[toFix[i]];
			}
			if (!y.remVal(toFix[i] + offset, r)) {
				return false;
			}
		}

		if (y.setMinNotR(l + offset)) {
			Clause* r = nullptr;
			if (so.lazy) {
				r = Reason_new(l + 1);
				for (int i = 0; i < l; i++) {
					(*r)[i + 1] = x[i];
				}
			}
			if (!y.setMin(l + offset, r)) {
				return false;
			}
		}
		if (y.setMaxNotR(u + offset)) {
			Clause* r = nullptr;
			if (so.lazy) {
				r = Reason_new(2);
				(*r)[1] = ~x[u];
			}
			if (!y.setMax(u + offset, r)) {
				return false;
			}
		}

		if (y.isFixed()) {
			const int yl = static_cast<int>(y.getVal()) - offset;
			if (x[yl].setValNotR(true)) {
				Clause* r = nullptr;
				if (so.lazy) {
					r = Reason_new(2);
					(*r)[1] = y.getValLit();
				}
				if (!x[yl].setVal(true, r)) {
					return false;
				}
			}
		}
		const int nl = static_cast<int>(y.getMin());
		for (int i = ol - offset; i < nl - offset; i++) {
			if (x[i].setValNotR(false)) {
				Clause* r = nullptr;
				if (so.lazy) {
					r = Reason_new(2);
					(*r)[1] = y.getFMinLit(nl);
				}
				if (!x[i].setVal(false, r)) {
					return false;
				}
			}
		}

		return true;
	}
};

void bool_arg_max(vec<BoolView>& x, int offset, IntVar* y) {
	vec<BoolView> w;
	for (unsigned int i = 0; i < x.size(); i++) {
		w.push(BoolView(x[i]));
	}
	new BoolArgMax(w, offset, IntView<>(y));
}
