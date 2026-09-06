#ifndef DDC_DECIMATOR_H_
#define DDC_DECIMATOR_H_

#include <stdint.h>
#include "complex_math.h"
#include "dds.h"

#define DECIMATION_FACTOR 16
#define FIR_TAPS 31 // Длина фильтра (нечетная, для симметрии)


int process_sample_downconvert_and_decimate(cplx_i16 ADC_In, DDS_Core_t *dds, cplx_i16 *LowFreq_Out);


#endif /* DDC_DECIMATOR_H_ */
