#ifndef __IIR_H__
#define __IIR_H__
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#define NUM_ORDERS            2

/* IIR coefficient struct */
struct IIR_coefficient
{
    float b[NUM_ORDERS + 1];
    float a[NUM_ORDERS]; 
    
};

/* IIR delay state struct */
struct IIR_state
{
    float w[NUM_ORDERS]; 
};

/**                                                                                                                                                                                         
  * @brief Initialize IIR filter state  
  * @param pState: Pointer to IIR state struct                                                                                                                                               
  * @return 0 on success, negative on fail                                                                                                                                                   
  */  
int iir_init(struct IIR_state *pState);

/**                                                                                                                                                                                         
 * @brief Apply single biquad stage IIR bandpass filter (Direct Form II)                                                                                                                    
 * @param input: Input sample                                                                                                                                                               
 * @param pCoef: Pointer to IIR coefficient struct                                                                                                                                          
 * @param pState: Pointer to IIR state struct                                                                                                                                               
 * @param pOutput: Pointer to filtered output sample                                                                                                                                        
 * @return 0 on success, negative on fail                                                                                                                                                   
 */
int iir_bandpass(float input, 
                 struct IIR_coefficient *pCoef,
                 struct IIR_state *pState, 
                 float *pOutput);
#endif