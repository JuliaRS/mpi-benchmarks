#pragma once

typedef void (*original_benchmark_func_t)(struct comm_info* c_info, int size,
                struct iter_schedule* ITERATIONS, MODES RUN_MODE, double* time);


enum descr_t { 
    REDUCTION, SELECT_SOURCE, 
    SINGLE_TRANSFER, PARALLEL_TRANSFER, COLLECTIVE, 
    PARALLEL_TRANSFER_MSG_RATE, SYNC,
    SCALE_TIME_HALF, SCALE_BW_DOUBLE, SCALE_BW_FOUR,
    SENDBUF_SIZE_I, SENDBUF_SIZE_2I, SENDBUF_SIZE_NP_I, SENDBUF_SIZE_0,
    RECVBUF_SIZE_I, RECVBUF_SIZE_2I, RECVBUF_SIZE_NP_I, RECVBUF_SIZE_0,
    HAS_ROOT,
    DEFAULT
};

struct LEGACY_GLOBALS {
    int NP_min;
    int NP, iter, size, ci_np;
    int header, MAXMSG;
    int x_sample, n_sample;
    Type_Size unit_size;
    double time[MAX_TIME_ID];
};

struct reworked_Bmark_descr {
    typedef std::set<descr_t> descr_set;
    descr_set flags;
    std::vector<std::string> comments; 
    std::vector<const char *> cmt; 
    bool stop_iterations;
    int time_limit[2];
    double sample_time;
    BTYPES descr2type(descr_t t) {
        switch(t) {
            case SINGLE_TRANSFER:
                return SingleTransfer;
            case PARALLEL_TRANSFER:
                return ParallelTransfer;
            case COLLECTIVE:
                return Collective;
            case PARALLEL_TRANSFER_MSG_RATE:
                return ParallelTransferMsgRate;
            case SYNC:
                return Sync;

        }
        return BTYPE_INVALID;
    }
    size_t descr2len(descr_t t, size_t i, size_t np) {
        switch(t) {
            case SENDBUF_SIZE_I:
                return i;
            case SENDBUF_SIZE_2I:
                return (size_t)2 * i;
            case SENDBUF_SIZE_NP_I:
                return np * i;
            case SENDBUF_SIZE_0:
                return 0;
            case RECVBUF_SIZE_I:
                return i;
            case RECVBUF_SIZE_2I:
                return (size_t)2 * i;
            case RECVBUF_SIZE_NP_I:
                return np * i;
            case RECVBUF_SIZE_0:
                return 0;
        }
        throw std::logic_error("descr2len: unknown len");
        return 0;
    }

    bool is_default() {
        return flags.count(DEFAULT) > 0;
    }

    bool IMB_set_bmark(struct Bench* Bmark, original_benchmark_func_t fn)
    {
        bool result = true;
        Bmark->N_Modes = 1;
        Bmark->RUN_MODES[0].AGGREGATE   =-1;
        Bmark->RUN_MODES[0].NONBLOCKING = 0;

        Bmark->reduction = (flags.count(REDUCTION) > 0);
        Bmark->Ntimes = 1;
        Bmark->select_source = (flags.count(SELECT_SOURCE) > 0);

        Bmark->Benchmark = fn;    
        for (size_t i = 0; i < comments.size(); i++) {
            cmt.push_back(comments[i].c_str());
        } 
        cmt.push_back(NULL);
        Bmark->bench_comments = const_cast<char **>(&cmt[0]);

        descr_set types;
        types.insert(SINGLE_TRANSFER); 
        types.insert(PARALLEL_TRANSFER); 
        types.insert(COLLECTIVE);
        types.insert(PARALLEL_TRANSFER_MSG_RATE);
        types.insert(SYNC);
        bool found = false;
        for (descr_set::iterator it = types.begin(); it != types.end(); ++it) {
            if (flags.count(*it)) {
                if (found)
                    result = false;
                Bmark->RUN_MODES[0].type = descr2type(*it);
                found = true;
            }
        }
        if (!found) 
            result = false;
        Bmark->scale_time = 1.0;
        Bmark->scale_bw = 1.0;
        if (flags.count(SCALE_TIME_HALF)) {
            Bmark->scale_time = 0.5;
        }
        if (flags.count(SCALE_BW_DOUBLE)) {
            Bmark->scale_bw = 2.0;
        }
        if (flags.count(SCALE_BW_FOUR)) {
            Bmark->scale_bw = 4.0;
        }
        return result;
    }

    smart_ptr<Scope> helper_init_scope(struct comm_info &c_info,
                                       struct Bench* Bmark, LEGACY_GLOBALS &glob) {
        NPLenCombinedScope &scope = *(new NPLenCombinedScope);
        int len = 0;
        int iter = 0;
        bool stop = false;
        while (true) {
            if (!(((c_info.n_lens == 0 && len < glob.MAXMSG ) ||
                 (c_info.n_lens > 0  && iter < c_info.n_lens))))
                break;
            if (stop)
                break;

            // --- helper_get_next_size(c_info, glob);
            if (c_info.n_lens > 0) {
                len = c_info.msglen[iter];
            } else {
                if( iter == 0 ) {
                    len = 0;
                } else if (iter == 1) {
                    len = ((1<<c_info.min_msg_log) + glob.unit_size - 1)/glob.unit_size*glob.unit_size;
                } else {
                    len = min(glob.MAXMSG, len + len);
                }
            }

            // --- helper_adjust_size(c_info, glob);
            if (len > glob.MAXMSG) {
                len = glob.MAXMSG;
            }
            len = (len + glob.unit_size - 1)/glob.unit_size*glob.unit_size;
            // --- helper_post_step(glob, BMark);
            iter++;
            if (Bmark->RUN_MODES[0].type == Sync || Bmark->RUN_MODES[0].type == SingleElementTransfer) {
                stop = true;
            }
            scope.add_len(len);
        }
       
        {
            int &NP_min = glob.NP_min;
            int &ci_np = c_info.w_num_procs;
            if (Bmark->RUN_MODES[0].type == ParallelTransferMsgRate) {
                ci_np -= ci_np % 2;
                NP_min += NP_min % 2;
            }
            int NP = max(1, min(ci_np, NP_min));
            bool do_it = true;
            while (do_it) {
                if (Bmark->RUN_MODES[0].type == SingleTransfer) {
                    NP = min(2, ci_np);
                }
                //std::cout << ">> " << ci_np << " " << NP << std::endl;
                scope.add_np(NP);
                
                // CALCULATE THE NUMBER OF PROCESSES FOR NEXT STEP
                if (NP >= ci_np) { do_it = false; }
                else {
                    NP = min(NP + NP, ci_np);
                }
            }
        }
        scope.commit();
        return smart_ptr<Scope>(&scope);
    }

    void IMB_init_buffers_iter(struct comm_info* c_info, struct iter_schedule* ITERATIONS,
                               struct Bench* Bmark, MODES BMODE, int iter, int size)
    {

    /* IMB 3.1 << */
        size_t s_len, r_len, s_alloc, r_alloc;
        int init_size, irep, i_s, i_r, x_sample;


//----------------------------------------------------------------------
        const bool root_based = (flags.count(HAS_ROOT) > 0);
//----------------------------------------------------------------------
//
//
//
//
// --- STEP 1: set x_sample and ITERATIONS->n_sample
        x_sample = BMODE->AGGREGATE ? ITERATIONS->msgspersample : ITERATIONS->msgs_nonaggr;

        /* July 2002 fix V2.2.1: */
#if (defined EXT || defined MPIIO || RMA)
        if( Bmark->access==no ) x_sample=ITERATIONS->msgs_nonaggr;
#endif

        Bmark->sample_failure = 0;

        init_size = max(size, asize);

        if (c_info->rank < 0) {
            return;
        } 

        if (ITERATIONS->iter_policy == imode_off) {
            ITERATIONS->n_sample = x_sample = ITERATIONS->msgspersample;
        } else if ((ITERATIONS->iter_policy == imode_multiple_np) || 
                   (ITERATIONS->iter_policy == imode_auto && root_based)) {
            /* n_sample for benchmarks with uneven distribution of works
               must be greater or equal and multiple to num_procs.
               The formula below is a negative leg of hyperbola.
               It's moved and scaled relative to max message size
               and initial n_sample subject to multiple to num_procs.
            */
            double d_n_sample = ITERATIONS->msgspersample;
            int max_msg_size = 1<<c_info->max_msg_log;
            int tmp = (int)(d_n_sample*max_msg_size/(c_info->num_procs*init_size+max_msg_size)+0.5);
            ITERATIONS->n_sample = x_sample = max(tmp-tmp%c_info->num_procs, c_info->num_procs);
        } else {
            ITERATIONS->n_sample = (size > 0)
                             ? max(1, min(ITERATIONS->overall_vol / size, x_sample))
                             : x_sample;
        }
// --- STEP 2: set s_len and r_len
//---------------------------------------------------------------------------------------------------
        bool result = true;
        {
            descr_set types;
            types.insert(SENDBUF_SIZE_I);
            types.insert(SENDBUF_SIZE_2I);
            types.insert(SENDBUF_SIZE_NP_I);
            types.insert(SENDBUF_SIZE_0);
            bool found = false;
            for (descr_set::iterator it = types.begin(); it != types.end(); ++it) {
                if (flags.count(*it)) {
                    if (found)
                        result = false;
                    s_len = descr2len(*it, (size_t)init_size,  (size_t)c_info->num_procs);
                    found = true;
                }
            }
            if (!found)
                result = false;
        }
        {
            descr_set types;
            types.insert(RECVBUF_SIZE_I);
            types.insert(RECVBUF_SIZE_2I);
            types.insert(RECVBUF_SIZE_NP_I);
            types.insert(RECVBUF_SIZE_0);
            bool found = false;
            for (descr_set::iterator it = types.begin(); it != types.end(); ++it) {
                if (flags.count(*it)) {
                    if (found)
                        result = false;
                    r_len = descr2len(*it, (size_t)init_size,  (size_t)c_info->num_procs);
                    found = true;
                }
            }
            if (!found)
                result = false;
        }
        if (!result) {
            throw std::logic_error("wrong recv or send buffer requirement description on a benchmark"); 
        }
//        printf(">> s_len=%ld, r_len=%ld\n", s_len, r_len);
//---------------------------------------------------------------------------------------------------
// --- STEP 3: set s_alloc and r_alloc AND all these ITERATIONS->s_offs,r_offs,...
//---------------------------------------------------------------------------------------------------
 
        /* IMB 3.1: new memory management for -off_cache */
        if (BMODE->type == Sync) {
            ITERATIONS->use_off_cache=0;
            ITERATIONS->n_sample=x_sample;
        } else {
#ifdef MPIIO
            ITERATIONS->use_off_cache=0;
#else
            ITERATIONS->use_off_cache = ITERATIONS->off_cache;
#endif
            if (ITERATIONS->off_cache) {
                if ( ITERATIONS->cache_size > 0) {
                    size_t cls = (size_t) ITERATIONS->cache_line_size;
                    size_t ofs = ( (s_len + cls - 1) / cls + 1 ) * cls;
                    ITERATIONS->s_offs = ofs;
                    ITERATIONS->s_cache_iter = min(ITERATIONS->n_sample,(2*ITERATIONS->cache_size*CACHE_UNIT+ofs-1)/ofs);
                    ofs = ( ( r_len + cls -1 )/cls + 1 )*cls;
                    ITERATIONS->r_offs = ofs;
                    ITERATIONS->r_cache_iter = min(ITERATIONS->n_sample,(2*ITERATIONS->cache_size*CACHE_UNIT+ofs-1)/ofs);
                } else {
                    ITERATIONS->s_offs=ITERATIONS->r_offs=0;
                    ITERATIONS->s_cache_iter=ITERATIONS->r_cache_iter=1;
                }
            }
        }

#ifdef MPIIO
        s_alloc = s_len;
        r_alloc = r_len;
#else
        if( ITERATIONS->use_off_cache ) {
            s_alloc = max(s_len,ITERATIONS->s_cache_iter*ITERATIONS->s_offs);
            r_alloc = max(r_len,ITERATIONS->r_cache_iter*ITERATIONS->r_offs);
        } else {
            s_alloc = s_len;
            r_alloc = r_len;
        }
#endif

// --- STEP 4: detect too much memory situation
//--------------------------------------------------------------------------------        
        c_info->used_mem = 1.f*(s_alloc+r_alloc)/MEM_UNIT;

#ifdef DEBUG
        {
            size_t mx, mu;

            mx = (size_t) MEM_UNIT*c_info->max_mem;
            mu = (size_t) MEM_UNIT*c_info->used_mem;

            DBG_I3("Got send / recv lengths; iters ",s_len,r_len,ITERATIONS->n_sample);
            DBG_I2("max  / used memory ",mx,mu);
            DBG_I2("send / recv offsets ",ITERATIONS->s_offs, ITERATIONS->r_offs);
            DBG_I2("send / recv cache iterations ",ITERATIONS->s_cache_iter, ITERATIONS->r_cache_iter);
            DBG_I2("send / recv buffer allocations ",s_alloc, r_alloc);
            DBGF_I2("Got send / recv lengths ",s_len,r_len);
            DBGF_I2("max  / used memory ",mx,mu);
            DBGF_I2("send / recv offsets ",ITERATIONS->s_offs, ITERATIONS->r_offs);
            DBGF_I2("send / recv cache iterations ",ITERATIONS->s_cache_iter, ITERATIONS->r_cache_iter);
            DBGF_I2("send / recv buffer allocations ",s_alloc, r_alloc);
        }
#endif

        if( c_info->used_mem > c_info->max_mem ) {
            Bmark->sample_failure=SAMPLE_FAILED_MEMORY;
            return;
        }


// --- call IMB_set_buf, IMB_init_transfer        
// -------------------------------------------------------------------------------------
        if (s_alloc > 0  && r_alloc > 0) {
            if (ITERATIONS->use_off_cache) {
                IMB_alloc_buf(c_info, "IMB_init_buffers_iter 1", s_alloc, r_alloc);
                IMB_set_buf(c_info, c_info->rank, 0, s_len-1, 0, r_len-1);

                for (irep = 1; irep < ITERATIONS->s_cache_iter; irep++) {
                    i_s = irep % ITERATIONS->s_cache_iter;
                    memcpy((void*)((char*)c_info->s_buffer + i_s * ITERATIONS->s_offs), c_info->s_buffer, s_len);
                }

                for (irep = 1; irep < ITERATIONS->r_cache_iter; irep++) {
                    i_r = irep % ITERATIONS->r_cache_iter;
                    memcpy((void*)((char*)c_info->r_buffer + i_r * ITERATIONS->r_offs), c_info->r_buffer, r_len);
                }
            } else {
                IMB_set_buf(c_info, c_info->rank, 0, s_alloc-1, 0, r_alloc-1);
            }
        }

        IMB_init_transfer(c_info, Bmark, size, (MPI_Aint) max(s_alloc, r_alloc));


// --- change  ITERATIONS->n_sample
// -------------------------------------------------------------------------------------
//
        /* Determine #iterations if dynamic adaptation requested */
        if ((ITERATIONS->iter_policy == imode_dynamic) || (ITERATIONS->iter_policy == imode_auto && !root_based)) {
            double time[MAX_TIME_ID];
            int acc_rep_test, t_sample;
            int selected_n_sample = ITERATIONS->n_sample;

            memset(time, 0, MAX_TIME_ID);
            if (iter == 0 || BMODE->type == Sync) {
                ITERATIONS->n_sample_prev = ITERATIONS->msgspersample;
                if (c_info->n_lens > 0) {
                    memset(ITERATIONS->numiters, 0, c_info->n_lens);
                }
            }

            /* first, run 1 iteration only */
            ITERATIONS->n_sample=1;
#ifdef MPI1
            c_info->select_source = Bmark->select_source;
#endif
            Bmark->Benchmark(c_info,size,ITERATIONS,BMODE,&time[0]);

            time[1] = time[0];

#ifdef MPIIO
            if( Bmark->access != no) {
                ierr = MPI_File_seek(c_info->fh, 0 ,MPI_SEEK_SET);
                MPI_ERRHAND(ierr);

                if( Bmark->fpointer == shared) {
                    ierr = MPI_File_seek_shared(c_info->fh, 0 ,MPI_SEEK_SET);
                    MPI_ERRHAND(ierr);
                }
            }
#endif /*MPIIO*/

            MPI_Allreduce(&time[1], &time[0], 1, MPI_DOUBLE, MPI_MAX, c_info->communicator);

            { /* determine rough #repetitions for a run time of 1 sec */
                int rep_test = 1;
                if (time[0] < (1.0 / MSGSPERSAMPLE)) {
                    rep_test = MSGSPERSAMPLE;
                } else if ((time[0] < 1.0)) {
                    rep_test = (int)(1.0 / time[0] + 0.5);
                }

                MPI_Allreduce(&rep_test, &acc_rep_test, 1, MPI_INT, MPI_MAX, c_info->communicator);
            }

            ITERATIONS->n_sample = min(selected_n_sample, acc_rep_test);

            if (ITERATIONS->n_sample > 1) {
#ifdef MPI1
                c_info->select_source = Bmark->select_source;
#endif
                Bmark->Benchmark(c_info,size,ITERATIONS,BMODE,&time[0]);
                time[1] = time[0];
#ifdef MPIIO
                if( Bmark->access != no) {
                    ierr = MPI_File_seek(c_info->fh, 0 ,MPI_SEEK_SET);
                    MPI_ERRHAND(ierr);

                    if ( Bmark->fpointer == shared) {
                        ierr = MPI_File_seek_shared(c_info->fh, 0 ,MPI_SEEK_SET);
                        MPI_ERRHAND(ierr);
                    }
                }
#endif /*MPIIO*/

                MPI_Allreduce(&time[1], &time[0], 1, MPI_DOUBLE, MPI_MAX, c_info->communicator);
            }

            {
                float val = (float) (1+ITERATIONS->secs/time[0]);
                t_sample = (time[0] > 1.e-8 && (val <= (float) 0x7fffffff))
                            ? (int)val
                            : selected_n_sample;
            }

            if (c_info->n_lens>0 && BMODE->type != Sync) {
                // check monotonicity with msg sizes
                int i;
                for (i = 0; i < iter; i++) {
                    t_sample = ( c_info->msglen[i] < size )
                                ? min(t_sample,ITERATIONS->numiters[i])
                                : max(t_sample,ITERATIONS->numiters[i]);
                }
                ITERATIONS->n_sample = ITERATIONS->numiters[iter] = min(selected_n_sample, t_sample);
            } else {
                ITERATIONS->n_sample = min(selected_n_sample,
                                            min(ITERATIONS->n_sample_prev, t_sample));
            }

            MPI_Bcast(&ITERATIONS->n_sample, 1, MPI_INT, 0, c_info->communicator);

#ifdef DEBUG
            {
                int usec=time*1000000;

                DBGF_I2("Checked time with #iters / usec ",acc_rep_test,usec);
                DBGF_I1("=> # samples, aligned with previous ",t_sample);
                DBGF_I1("final #samples ",ITERATIONS->n_sample);
            }
#endif
            
// --- call Benchmark
// -------------------------------------------------------------------------------------
//
 
        } else { /*if( (ITERATIONS->iter_policy == imode_dynamic) || (ITERATIONS->iter_policy == imode_auto && !root_based) )*/
            double time[MAX_TIME_ID];
            Bmark->Benchmark(c_info,size,ITERATIONS,BMODE,&time[0]);
        }

// --- save n_sample_prev
// -------------------------------------------------------------------------------------
//
         ITERATIONS->n_sample_prev=ITERATIONS->n_sample;

    /* >> IMB 3.1  */

    }

    void helper_sync_legacy_globals_1(comm_info &c_info, LEGACY_GLOBALS &glob, 
struct Bench *Bmark) {
        // NP_min is already initialized by IMB_basic_input
        glob.ci_np = c_info.w_num_procs;
        if (Bmark->RUN_MODES[0].type == ParallelTransferMsgRate) {
            glob.ci_np -= glob.ci_np % 2;
            glob.NP_min += glob.NP_min % 2;
        }
        glob.NP=max(1,min(glob.ci_np,glob.NP_min));
        if (Bmark->RUN_MODES[0].type == SingleTransfer) {
            glob.NP = (min(2,glob.ci_np));
        }
        if (Bmark->reduction ||
            Bmark->RUN_MODES[0].type == SingleElementTransfer) {
            MPI_Type_size(c_info.red_data_type,&glob.unit_size);
        } else {
            MPI_Type_size(c_info.s_data_type,&glob.unit_size);
        }
    }

    void helper_sync_legacy_globals_2(comm_info &c_info, LEGACY_GLOBALS &glob,
struct Bench *Bmark) {
        glob.MAXMSG=(1<<c_info.max_msg_log)/glob.unit_size * glob.unit_size;
        glob.header=1;
        Bmark->sample_failure = 0;
        sample_time = MPI_Wtime();
        time_limit[0] = time_limit[1] = 0;
        Bmark->success = 1;
        c_info.select_source = Bmark->select_source;
        stop_iterations = false;
        glob.iter = 0;
        glob.size = 0;
        if (Bmark->RUN_MODES[0].type == SingleElementTransfer) {
            /* just one size needs to be tested (the size of one element) */
            MPI_Type_size(c_info.red_data_type, &glob.size);
        }
//        if (Bmark->RUN_MODES[0].type == BTYPE_INVALID)
//            stop_iterations = true;
    }

    void helper_time_check(comm_info &c_info, LEGACY_GLOBALS &glob, 
                           Bench *Bmark, iter_schedule &ITERATIONS) {
        if (!Bmark->sample_failure) {
            time_limit[1] = 0;
            if (c_info.rank >= 0) {
                time_limit[1] = (MPI_Wtime() - sample_time < max(max(c_info.n_lens, c_info.max_msg_log - c_info.min_msg_log) - 1, 1) * ITERATIONS.secs) ? 0 : 1;
            }
        }
        MPI_Allreduce(&time_limit[1], &time_limit[0], 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        if (time_limit[0]) {
            Bmark->sample_failure = SAMPLE_FAILED_TIME_OUT;
            stop_iterations = true;
        }
        return;
    }
};
