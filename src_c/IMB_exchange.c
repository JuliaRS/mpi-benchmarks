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

[1] doc/ReadMe_IMB.txt 

[2] Intel(R) MPI Benchmarks
    Users Guide and Methodology Description
    In 
    doc/IMB_Users_Guide.pdf

 File: IMB_exchange.c 

 Implemented functions: 

 IMB_exchange;

 ***************************************************************************/





#include "IMB_declare.h"
#include "IMB_benchmark.h"

#include "IMB_prototypes.h"


/*************************************************************************/

/* ===================================================================== */
/* 
IMB 3.1 changes
July 2007
Hans-Joachim Plum, Intel GmbH

- replace "int n_sample" by iteration scheduling object "ITERATIONS"
  (see => IMB_benchmark.h)

- proceed with offsets in send / recv buffers to eventually provide
  out-of-cache data
*/
/* ===================================================================== */


void IMB_exchange(struct comm_info* c_info, int size, struct iter_schedule* ITERATIONS,
                  MODES RUN_MODE, double* time) {
/*

                      MPI-1 benchmark kernel
                      Chainwise exchange; MPI_Isend (left+right) + MPI_Recv (right+left)

Input variables:

-c_info               (type struct comm_info*)
                      Collection of all base data for MPI;
                      see [1] for more information

-size                 (type int)
                      Basic message size in bytes

-ITERATIONS           (type struct iter_schedule *)
                      Repetition scheduling

-RUN_MODE             (type MODES)
                      (only MPI-2 case: see [1])

Output variables:

-time                 (type double*)
                      Timing result per sample


*/
    int  i;

    Type_Size s_size, r_size;
    int s_num = 0,
        r_num = 0;
    int s_tag, r_tag;
    int left, right;
    MPI_Status  stat[2];
    MPI_Request request[2];

#ifdef CHECK
    defect = 0;
#endif

    /*GET SIZE OF DATA TYPE's in s_size and r_size*/
    MPI_Type_size(c_info->s_data_type, &s_size);
    MPI_Type_size(c_info->r_data_type, &r_size);
    if ((s_size != 0) && (r_size != 0))
    {
        s_num = size / s_size;
        r_num = size / r_size;
    }
    s_tag = 1;
    r_tag = c_info->select_tag ? s_tag : MPI_ANY_TAG;

    size *= c_info->size_scale;

    *time = 0;
    if (c_info->rank != -1) {
        if (c_info->rank < c_info->num_procs - 1)   right = c_info->rank + 1;
        if (c_info->rank > 0)                       left = c_info->rank - 1;
        if (c_info->rank == c_info->num_procs - 1)  right = 0;
        if (c_info->rank == 0)                      left = c_info->num_procs - 1;

        if ((c_info->rank >= 0) && (c_info->rank <= c_info->num_procs - 1)) {
            for (i = 0; i < N_BARR; i++)
                MPI_Barrier(c_info->communicator);

            *time -= MPI_Wtime();
            for (i = 0; i < ITERATIONS->n_sample; i++) {
                MPI_ERRHAND(MPI_Isend((char*)c_info->s_buffer + i % ITERATIONS->s_cache_iter * ITERATIONS->s_offs,
                                      s_num, c_info->s_data_type,
                                      right, s_tag, c_info->communicator, &request[0]));
                MPI_ERRHAND(MPI_Isend((char*)c_info->s_buffer + size + i % ITERATIONS->s_cache_iter * ITERATIONS->s_offs,
                                      s_num, c_info->s_data_type,
                                      left, s_tag, c_info->communicator, &request[1]));

                MPI_ERRHAND(MPI_Recv((char*)c_info->r_buffer + i%ITERATIONS->r_cache_iter * ITERATIONS->r_offs,
                                     r_num, c_info->r_data_type,
                                     left, r_tag, c_info->communicator, stat));

                CHK_DIFF("Exchange", c_info, (char*)c_info->r_buffer + i % ITERATIONS->r_cache_iter * ITERATIONS->r_offs,
                         0, size, size, 1,
                         put, 0, ITERATIONS->n_sample, i,
                         left, &defect);

                MPI_ERRHAND(MPI_Recv((char*)c_info->r_buffer + i % ITERATIONS->r_cache_iter * ITERATIONS->r_offs,
                                     r_num, c_info->r_data_type,
                                     right, r_tag, c_info->communicator, stat));

                CHK_DIFF("Exchange", c_info, (char*)c_info->r_buffer + i % ITERATIONS->r_cache_iter * ITERATIONS->r_offs,
                         s_num, size, size, 1,
                         put, 0, ITERATIONS->n_sample, i,
                         right, &defect);

                MPI_ERRHAND(MPI_Waitall(2, request, stat));
            }
            *time += MPI_Wtime();
        }
    }
    *time /= ITERATIONS->n_sample;
}


