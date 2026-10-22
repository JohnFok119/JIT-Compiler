/* Tiny zero-dependency test harness. One main() per test file. */

#ifndef CHECK_H
#define CHECK_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int check_total_;
static int check_failed_;

#define CHECK(cond)							\
	do {								\
		++check_total_;						\
		if (!(cond)) {						\
			++check_failed_;				\
			printf("  FAIL %s:%d: %s\n",			\
			       __FILE__, __LINE__, #cond);		\
		}							\
	} while (0)

#define CHECK_EQ_DBL_BITS(a, b)						\
	do {								\
		double a_ = (a);					\
		double b_ = (b);					\
		uint64_t ab_, bb_;					\
		memcpy(&ab_, &a_, sizeof (ab_));			\
		memcpy(&bb_, &b_, sizeof (bb_));			\
		++check_total_;						\
		if (ab_ != bb_) {					\
			++check_failed_;				\
			printf("  FAIL %s:%d: %s == %s: "		\
			       "%.17g (0x%016llx) vs %.17g (0x%016llx)\n", \
			       __FILE__, __LINE__, #a, #b,		\
			       a_, (unsigned long long)ab_,		\
			       b_, (unsigned long long)bb_);		\
		}							\
	} while (0)

#define CHECK_STR(a, b)							\
	do {								\
		const char *as_ = (a);					\
		const char *bs_ = (b);					\
		++check_total_;						\
		if (!as_ || !bs_ || strcmp(as_, bs_)) {			\
			++check_failed_;				\
			printf("  FAIL %s:%d: %s == %s: \"%s\" vs \"%s\"\n", \
			       __FILE__, __LINE__, #a, #b,		\
			       as_ ? as_ : "(null)",			\
			       bs_ ? bs_ : "(null)");			\
		}							\
	} while (0)

/* Call at end of main(); returns process exit status. */
#define CHECK_REPORT(file)						\
	(check_failed_							\
	 ? (printf("FAIL %s: %d of %d checks failed\n",			\
		   (file), check_failed_, check_total_), 1)		\
	 : (printf("PASS %s: %d checks\n", (file), check_total_), 0))

#endif /* CHECK_H */
