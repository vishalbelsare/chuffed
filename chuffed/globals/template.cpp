#include "chuffed/core/propagator.h"
#include "chuffed/support/vec.h"
#include "chuffed/vars/int-view.h"
#include "chuffed/vars/vars.h"

class BinPacking : public Propagator {
public:
	// constant data
	vec<IntView<0> > x;

	// Persistent trailed state

	//	...

	// Persistent non-trailed state

	//	...

	// Intermediate state

	//	...

	BinPacking(vec<IntView<0> >& _x) : x(_x) {
		// set priority
		priority = 2;
		// attach to var events
		for (unsigned int i = 0; i < x.size(); i++) {
			x[i].attach(this, i, EVENT_F);
		}
	}

	void wakeup(int /*i*/, int /*c*/) override { pushInQueue(); }

	bool propagate() override { return true; }

	void clearPropState() override { in_queue = false; }
};
