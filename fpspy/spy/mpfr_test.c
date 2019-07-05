#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <gmp.h>
#include <mpfr.h>

// built in random number generator to avoid changing the state
// of the application's random number generator
//
// This is borrowed from NK and should probably be replaced
//
static uint64_t xi=0;

static void seed_rand(uint64_t seed)
{
    xi = seed;
}

// linear congruent, full 64 bit space
static inline uint64_t _pump_rand(uint64_t xi, uint64_t a, uint64_t c)
{
    uint64_t xi_new = (a*xi + c);

    return xi_new;
}    

static inline uint64_t pump_rand()
{
    xi = _pump_rand(xi, 0x5deece66dULL, 0xbULL);
    
    return xi;
}

static inline uint64_t get_rand()
{
    return pump_rand();
}



// rate in us, return in us
static uint64_t next_exp(uint64_t mean_us)
{
    mpfr_t u, t;
    
    mpfr_init2(u,64);
    mpfr_init2(t,64);
    
    uint64_t r = get_rand(); r &= (~1ULL - 1);
    mpfr_set_ui(u,r,MPFR_RNDN);
    mpfr_set_ui(t,(uint64_t)-1,MPFR_RNDN);
    mpfr_div(u,u,t,MPFR_RNDN);

    // u now is [0..1)

    mpfr_set_ui(t,1,MPFR_RNDN);
    mpfr_sub(u,t,u,MPFR_RNDN);
    
    // u now is 1-[0..1)

    mpfr_log(u,u,MPFR_RNDN);

    // u now is log(1-[0..1))

    mpfr_set_ui(t,mean_us,MPFR_RNDN);
    
    mpfr_mul(u,u,t,MPFR_RNDN);

    mpfr_neg(u,u,MPFR_RNDN);

    // u is now -log(1-[0..1))*mean_us

    // peel off the result in us:
    uint64_t result = mpfr_get_ui(u,MPFR_RNDN);

    return result;
}


int main()
{
    int i;
    
    seed_rand(9453948);
	
    for (i=0;i<100000;i++) {
	printf("%lu\n",next_exp(1000));
    }
      
}
