/****************************************************************************
*                                                                           *
* Copyright (C) 2024 Intel Corporation                                      *
*                                                                           *
*****************************************************************************

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 ***************************************************************************

For more documentation than found here, see

[1] doc/ReadMe_IMB.txt 

[2] Intel(R) MPI Benchmarks
    Users Guide and Methodology Description
    In 
    doc/IMB_Users_Guide.pdf
    
 File: IMB_reduce_local.c 

 Implemented functions: 

 IMB_reduce_local;

 ***************************************************************************/





#include "IMB_declare.h"
#include "IMB_benchmark.h"

#include "IMB_prototypes.h"

#ifdef MPI1

/*******************************************************************************/


void IMB_reduce_local(struct comm_info* c_info, int size, struct iter_schedule* ITERATIONS,
                MODES RUN_MODE, double* time) {
/*

                          MPI-1 benchmark kernel
                          Benchmarks MPI_Reduce_local

Input variables:

-c_info                   (type struct comm_info*)
                          Collection of all base data for MPI;
                          see [1] for more information

-size                     (type int)
                          Basic message size in bytes

-ITERATIONS               (type struct iter_schedule *)
                          Repetition scheduling

-RUN_MODE                 (type MODES)
                          (only MPI-2 case: see [1])

Output variables:

-time                     (type double*)
                          Timing result per sample

*/
    int    i;
    Type_Size s_size;
    int s_num = 0;
    double t1, t2;

#ifdef CHECK
    int asize = (int) sizeof(assign_type);
    defect = 0.;
#endif

    /*  GET SIZE OF DATA TYPE */
    MPI_Type_size(c_info->red_data_type, &s_size);

    if (s_size != 0)
        s_num = size / s_size;

    size *= c_info->size_scale;
    *time = 0.;
    if (c_info->rank != -1) {
        IMB_do_n_barriers(c_info->communicator, N_BARR);

        for (i = 0; i < ITERATIONS->n_sample; i++) {
            t1 = MPI_Wtime();
            MPI_ERRHAND(MPI_Reduce_local((char*)c_info->s_buffer + i % ITERATIONS->s_cache_iter * ITERATIONS->s_offs,
                                         (char*)c_info->r_buffer + i % ITERATIONS->r_cache_iter * ITERATIONS->r_offs,
                                         s_num,
                                         c_info->red_data_type, c_info->op_type));
            t2 = MPI_Wtime();
            *time += (t2 - t1);
            CHK_DIFF("Reduce_local", c_info, (char*)c_info->r_buffer + i % ITERATIONS->r_cache_iter*ITERATIONS->r_offs, 0,
                     size, size, asize,
                     local, 0, ITERATIONS->n_sample, i,
                     -1, &defect);

            IMB_do_n_barriers(c_info->communicator, c_info->sync);
        }
        *time /= ITERATIONS->n_sample;
    }
}

#endif
