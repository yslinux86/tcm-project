/* bn_gcd.c */

#include "cryptlib.h"
#include "bn_lcl.h"

/* solves ax == 1 (mod n) */
BIGNUM *BN_mod_inverse(BIGNUM *in,
	const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx)
{
	BIGNUM *A,*B,*X,*Y,*M,*D,*T,*R=NULL;
	BIGNUM *ret=NULL;
	int sign;

	bn_check_top(a);
	bn_check_top(n);

	BN_CTX_start(ctx);
	A = BN_CTX_get(ctx);
	B = BN_CTX_get(ctx);
	X = BN_CTX_get(ctx);
	D = BN_CTX_get(ctx);
	M = BN_CTX_get(ctx);
	Y = BN_CTX_get(ctx);
	T = BN_CTX_get(ctx);
	if (T == NULL) goto err;

	if (in == NULL)
		R=BN_new();
	else
		R=in;
	if (R == NULL) goto err;

	BN_one(X);
	BN_zero(Y);
	if (BN_copy(B,a) == NULL) goto err;
	if (BN_copy(A,n) == NULL) goto err;
	A->neg = 0;
	if (B->neg || (BN_ucmp(B, A) >= 0))
	{
		if (!BN_nnmod(B, B, A, ctx)) goto err;
	}
	sign = -1;
	/* From  B = a mod |n|,  A = |n|  it follows that
	 *
	 *      0 <= B < A,
	 *     -sign*X*a  ==  B   (mod |n|),
	 *      sign*Y*a  ==  A   (mod |n|).
	 */

	if (BN_is_odd(n) && (BN_num_bits(n) <= (BN_BITS <= 32 ? 450 : 2048)))
	{
		/* Binary inversion algorithm; requires odd modulus.
		 * This is faster than the general algorithm if the modulus
		 * is sufficiently small (about 400 .. 500 bits on 32-bit
		 * sytems, but much more on 64-bit systems) */
		int shift;
		
		while (!BN_is_zero(B))
		{
			/*
			 *      0 < B < |n|,
			 *      0 < A <= |n|,
			 * (1) -sign*X*a  ==  B   (mod |n|),
			 * (2)  sign*Y*a  ==  A   (mod |n|)
			 */

			/* Now divide  B  by the maximum possible power of two in the integers,
			 * and divide  X  by the same value mod |n|.
			 * When we're done, (1) still holds. */
			shift = 0;
			while (!BN_is_bit_set(B, shift)) /* note that 0 < B */
			{
				shift++;
				
				if (BN_is_odd(X))
				{
					if (!BN_uadd(X, X, n)) goto err;
				}
				/* now X is even, so we can easily divide it by two */
				if (!BN_rshift1(X, X)) goto err;
			}
			if (shift > 0)
			{
				if (!BN_rshift(B, B, shift)) goto err;
			}


			/* Same for  A  and  Y.  Afterwards, (2) still holds. */
			shift = 0;
			while (!BN_is_bit_set(A, shift)) /* note that 0 < A */
			{
				shift++;
				
				if (BN_is_odd(Y))
				{
					if (!BN_uadd(Y, Y, n)) goto err;
				}
				/* now Y is even */
				if (!BN_rshift1(Y, Y)) goto err;
			}
			if (shift > 0)
			{
				if (!BN_rshift(A, A, shift)) goto err;
			}

			
			/* We still have (1) and (2).
			 * Both  A  and  B  are odd.
			 * The following computations ensure that
			 *
			 *     0 <= B < |n|,
			 *      0 < A < |n|,
			 * (1) -sign*X*a  ==  B   (mod |n|),
			 * (2)  sign*Y*a  ==  A   (mod |n|),
			 *
			 * and that either  A  or  B  is even in the next iteration.
			 */
			if (BN_ucmp(B, A) >= 0)
			{
				/* -sign*(X + Y)*a == B - A  (mod |n|) */
				if (!BN_uadd(X, X, Y)) goto err;
				/* NB: we could use BN_mod_add_quick(X, X, Y, n), but that
				 * actually makes the algorithm slower */
				if (!BN_usub(B, B, A)) goto err;
			}
			else
			{
				/*  sign*(X + Y)*a == A - B  (mod |n|) */
				if (!BN_uadd(Y, Y, X)) goto err;
				/* as above, BN_mod_add_quick(Y, Y, X, n) would slow things down */
				if (!BN_usub(A, A, B)) goto err;
			}
		}
	}
	else
	{
		/* general inversion algorithm */

		while (!BN_is_zero(B))
		{
			BIGNUM *tmp;
			
			/*
			 *      0 < B < A,
			 * (*) -sign*X*a  ==  B   (mod |n|),
			 *      sign*Y*a  ==  A   (mod |n|)
			 */
			
			/* (D, M) := (A/B, A%B) ... */
			if (BN_num_bits(A) == BN_num_bits(B))
			{
				if (!BN_one(D)) goto err;
				if (!BN_sub(M,A,B)) goto err;
			}
			else if (BN_num_bits(A) == BN_num_bits(B) + 1)
			{
				/* A/B is 1, 2, or 3 */
				if (!BN_lshift1(T,B)) goto err;
				if (BN_ucmp(A,T) < 0)
				{
					/* A < 2*B, so D=1 */
					if (!BN_one(D)) goto err;
					if (!BN_sub(M,A,B)) goto err;
				}
				else
				{
					/* A >= 2*B, so D=2 or D=3 */
					if (!BN_sub(M,A,T)) goto err;
					if (!BN_add(D,T,B)) goto err; /* use D (:= 3*B) as temp */
					if (BN_ucmp(A,D) < 0)
					{
						/* A < 3*B, so D=2 */
						if (!BN_set_word(D,2)) goto err;
						/* M (= A - 2*B) already has the correct value */
					}
					else
					{
						/* only D=3 remains */
						if (!BN_set_word(D,3)) goto err;
						/* currently  M = A - 2*B,  but we need  M = A - 3*B */
						if (!BN_sub(M,M,B)) goto err;
					}
				}
			}
			else
			{
				if (!BN_div(D,M,A,B,ctx)) goto err;
			}
			
			/* Now
			 *      A = D*B + M;
			 * thus we have
			 * (**)  sign*Y*a  ==  D*B + M   (mod |n|).
			 */
			
			tmp=A; /* keep the BIGNUM object, the value does not matter */
			
			/* (A, B) := (B, A mod B) ... */
			A=B;
			B=M;
			/* ... so we have  0 <= B < A  again */
			
			/* Since the former  M  is now  B  and the former  B  is now  A,
			 * (**) translates into
			 *       sign*Y*a  ==  D*A + B    (mod |n|),
			 * i.e.
			 *       sign*Y*a - D*A  ==  B    (mod |n|).
			 * Similarly, (*) translates into
			 *      -sign*X*a  ==  A          (mod |n|).
			 *
			 * Thus,
			 *   sign*Y*a + D*sign*X*a  ==  B  (mod |n|),
			 * i.e.
			 *        sign*(Y + D*X)*a  ==  B  (mod |n|).
			 *
			 * So if we set  (X, Y, sign) := (Y + D*X, X, -sign),  we arrive back at
			 *      -sign*X*a  ==  B   (mod |n|),
			 *       sign*Y*a  ==  A   (mod |n|).
			 * Note that  X  and  Y  stay non-negative all the time.
			 */
			
			/* most of the time D is very small, so we can optimize tmp := D*X+Y */
			if (BN_is_one(D))
			{
				if (!BN_add(tmp,X,Y)) goto err;
			}
			else
			{
				if (BN_is_word(D,2))
				{
					if (!BN_lshift1(tmp,X)) goto err;
				}
				else if (BN_is_word(D,4))
				{
					if (!BN_lshift(tmp,X,2)) goto err;
				}
				else if (D->top == 1)
				{
					if (!BN_copy(tmp,X)) goto err;
					if (!BN_mul_word(tmp,D->d[0])) goto err;
				}
				else
				{
					if (!BN_mul(tmp,D,X,ctx)) goto err;
				}
				if (!BN_add(tmp,tmp,Y)) goto err;
			}
			
			M=Y; /* keep the BIGNUM object, the value does not matter */
			Y=X;
			X=tmp;
			sign = -sign;
		}
	}
		
	/*
	 * The while loop (Euclid's algorithm) ends when
	 *      A == gcd(a,n);
	 * we have
	 *       sign*Y*a  ==  A  (mod |n|),
	 * where  Y  is non-negative.
	 */

	if (sign < 0)
	{
		if (!BN_sub(Y,n,Y)) goto err;
	}
	/* Now  Y*a  ==  A  (mod |n|).  */
	

	if (BN_is_one(A))
	{
		/* Y*a == 1  (mod |n|) */
		if (!Y->neg && BN_ucmp(Y,n) < 0)
		{
			if (!BN_copy(R,Y)) goto err;
		}
		else
		{
			if (!BN_nnmod(R,Y,n,ctx)) goto err;
		}
	}
	else
	{
		goto err;
	}
	ret=R;
err:
	if ((ret == NULL) && (in == NULL)) BN_free(R);
	BN_CTX_end(ctx);
	return(ret);
}
