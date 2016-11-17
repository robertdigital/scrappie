#ifndef DECODE_H
#define DECODE_H
#include "util.h"

float decode_transducer(const Mat_rptr logpost, float skip_pen, int * seq);
char * overlapper(const int * seq, int n, int nkmer);

#endif  /* DECODE_H */
