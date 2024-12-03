#include "chuffed/primitives/primitives.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/bool-view.h"
#include "chuffed/vars/int-var.h"
#include "chuffed/vars/int-view.h"

void newBinGE(IntView<> x, IntView<> y, const BoolView& r);
void newBinNE(IntView<> x, IntView<> y, const BoolView& r);

// x lex< y

// b[i] = r[i] || b[i+1]
// b[i] -> x[i] <= y[i]
// r[i] -> x[i] < y[i]

void lex(vec<IntVar*>& x, vec<IntVar*>& y, bool strict) {
	vec<BoolView> b;
	vec<BoolView> r;
	b.push(bv_true);
	for (unsigned int i = 1; i < x.size(); i++) {
		b.push(newBoolVar());
	}
	b.push(bv_false);
	for (unsigned int i = 0; i < x.size(); i++) {
		r.push(newBoolVar());
	}

	for (unsigned int i = 0; i < x.size() - 1 + static_cast<unsigned int>(strict); i++) {
		bool_rel(r[i], BRT_OR, b[i + 1], b[i]);
		newBinGE(IntView<>(y[i]), IntView<>(x[i], 1, 1), r[i]);
	}
	for (unsigned int i = 0; i < x.size(); i++) {
		newBinGE(IntView<>(y[i]), IntView<>(x[i]), b[i]);
	}
}
