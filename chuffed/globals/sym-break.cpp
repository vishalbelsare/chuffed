#include "chuffed/core/propagator.h"
#include "chuffed/primitives/primitives.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/int-view.h"
#include "chuffed/vars/modelling.h"
#include "chuffed/vars/vars.h"

#include <cstdint>

void var_sym_break(vec<IntVar*>& x) {
	for (unsigned int i = 0; i < x.size() - 1; i++) {
		int_rel(x[i], IRT_LE, x[i + 1]);
	}
}

// (x = k /\ y = k) || (x < y)

class BinLTInf : public Propagator {
	IntView<0> x;
	IntView<0> y;
	int k;

public:
	BinLTInf(IntVar* _x, IntVar* _y, int _k) : x(_x), y(_y), k(_k) {
		x.attach(this, 0, EVENT_L);
		y.attach(this, 1, EVENT_U);
	}

	bool propagate() override {
		if (y.getMax() < k) {
			const int64_t m = y.getMax() - 1;
			setDom(x, setMax, m, y.getMaxLit());
		}

		int64_t m = x.getMin() + 1;
		if (m > k) {
			m = k;
		}
		setDom(y, setMin, m, x.getMinLit());

		return true;
	}
};

void val_sym_break(vec<IntVar*>& x, int l, int u) {
	vec<IntVar*> y;
	createVars(y, u - l + 1, 0, x.size(), true);
	for (unsigned int i = 0; i < x.size(); i++) {
		x[i]->specialiseToEL();
	}
	for (int i = l; i <= u; i++) {
		for (unsigned int j = 0; j < x.size(); j++) {
			bool_rel(y[i - l]->getLit(j, LR_EQ), BRT_R_IMPL, x[j]->getLit(i, LR_EQ));
			bool_rel(x[j]->getLit(i, LR_EQ), BRT_R_IMPL, y[i - l]->getLit(j, LR_LE));
		}
	}
	for (int i = 0; i < u - l; i++) {
		new BinLTInf(y[i], y[i + 1], x.size());
	}
}
