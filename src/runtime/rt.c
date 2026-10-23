#include <math.h>
#include "runtime/rt.h"

double
jit_rt_sigmoid(double x)
{
	return 1.0 / (1.0 + exp(-x));
}
