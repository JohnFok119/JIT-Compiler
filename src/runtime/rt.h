/* Runtime helpers shared by the interpreter and every backend. */

#ifndef JIT_RT_H
#define JIT_RT_H

/* 1.0 / (1.0 + exp(-x)), computed exactly in that order. */
double jit_rt_sigmoid(double x);

#endif /* JIT_RT_H */
