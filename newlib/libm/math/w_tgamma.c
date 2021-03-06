/* @(#)w_gamma.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

/* double gamma(double x)
 * Return  the logarithm of the Gamma function of x or the Gamma function of x,
 * depending on the library mode.
 */

#include "fdlibm.h"

#ifndef _DOUBLE_IS_32BITS

#if !defined(_IEEE_LIBM) || !defined(HAVE_ALIAS_ATTRIBUTE)
#ifdef __STDC__
	double tgamma(double x)
#else
	double tgamma(x)
	double x;
#endif
{
        double y;
	y = __ieee754_gamma(x);
#ifdef _IEEE_LIBM
	return y;
#else
	if(_LIB_VERSION == _IEEE_) return y;

	if(!finite(y)&&finite(x)) {
	  if(floor(x)==x&&x<=0.0)
	    return __kernel_standard(x,x,41); /* tgamma pole */
	  else
	    return __kernel_standard(x,x,40); /* tgamma overflow */
	}
	return y;
#endif
}
#endif

#endif /* defined(_DOUBLE_IS_32BITS) */
