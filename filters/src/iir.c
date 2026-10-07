#include <iir.h>


/**                                                                                                                                                                                         
  * @brief Initialize IIR filter state  
  * @param pState: Pointer to IIR state struct                                                                                                                                               
  * @return 0 on success, negative on fail                                                                                                                                                   
  */  
int iir_init(struct IIR_state *pState)
{
    if(pState == NULL) return -EINVAL;
    memset(pState, 0, sizeof(*pState));
    return 0;
}

/**                                                                                                                                                                                         
 * @brief Apply single biquad stage IIR bandpass filter (Direct Form II)                                                                                                                    
 * @param input: Input sample                                                                                                                                                               
 * @param pCoef: Pointer to IIR coefficient struct                                                                                                                                          
 * @param pState: Pointer to IIR state struct                                                                                                                                               
 * @param pOutput: Pointer to filtered output sample                                                                                                                                        
 * @return 0 on success, negative on fail                                                                                                                                                   
 */
int iir_bandpass(float input,  struct IIR_coefficient *pCoef, struct IIR_state *pState,  float *pOutput)
{ 
    if(pCoef == NULL || pState == NULL || pOutput == NULL) return -EINVAL;
    float w = input;
     /* Step 1: w[n] = x[n] - a1*w[n-1] - a2*w[n-2] - ... */ 
    for(uint8_t order = 0; order < NUM_ORDERS; order++)
    {
       w-=pCoef->a[order]*pState->w[order];
    }
    
   /* Step 2: y[n] = b0*w[n] + b1*w[n-1] + b2*w[n-2] + ... */  
    *pOutput = pCoef->b[0]*w;
    for(uint8_t order = 0; order < NUM_ORDERS; order++)
    {
        *pOutput += pCoef->b[order+1]*pState->w[order];
    }
   
    /* Step 3: shift delay line w[n-2] <- w[n-1] <- w[n] */ 
    for(int8_t order = NUM_ORDERS - 1; order > 0; order--)
    {
       pState->w[order] = pState->w[order - 1];
    }
    pState->w[0]=w;
    return 0;
}