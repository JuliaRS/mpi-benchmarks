#pragma once
#include <mpi.h>
#include <omp.h>

#include <iostream>

#include "benchmark.h"
#include "benchmark_suite.h"

#include "MT_suite.h"

using namespace std;

#define GLUE_TYPENAME(A,B) A,B
#define GET_GLOBAL_VEC(TYPE, NAME, i) { TYPE *p = (TYPE *)bs::get_internal_data_ptr(#NAME, i); memcpy(&NAME, p, sizeof(TYPE)); }
#define GET_GLOBAL(TYPE, NAME) { TYPE *p = (TYPE *)bs::get_internal_data_ptr(#NAME); memcpy(&NAME, p, sizeof(TYPE)); }

/*
template <typename T>
MPI_Datatype get_mpi_datatype();

template <>
MPI_Datatype get_mpi_datatype<char>() {
    return MPI_CHAR;
}

template <>
MPI_Datatype get_mpi_datatype<int>() {
    return MPI_INT;
}
*/

template <typename T>
class Allocator {
    protected:
    std::vector<T *> original_ptrs;
    public:
    virtual T *Alloc(size_t size) = 0;
    Allocator() {}
    virtual ~Allocator() { 
        for (size_t i = 0; i < original_ptrs.size(); i++) { 
            free(original_ptrs[i]); 
        } 
    }
    private:
    Allocator &operator=(const Allocator &) { return *this; }
    Allocator(const Allocator &) {}
};

template <typename T>
class AlignedAllocator : public Allocator<T> {
    public:
    size_t align;
    AlignedAllocator(size_t _align = 1) : align(_align) { }
    virtual T *Alloc(size_t size) {
        //size_t aligned_size = (size + align - 1) & ~(align - 1);
        //assert(aligned_size >= size);
        size_t size_with_spare_space = size + align;
        char *ptr = (char *)malloc(size_with_spare_space);
        memset(ptr, 0, size_with_spare_space);
        Allocator<T>::original_ptrs.push_back(ptr);
        size_t diff = align - ((size_t)(ptr) % (size_t)align);
        return (T *)(ptr + diff);
    }
    virtual ~AlignedAllocator() {};
};

static void normal_barrier()
{
    MPI_Barrier(MPI_COMM_WORLD);
}

static void special_barrier()
{
    int size = 0;
    int rank = 0;

    int mask = 0x1;
    int dst,src;

    int tmp = 0;

    MPI_Comm_size(MPI_COMM_WORLD, &size );
    MPI_Comm_rank(MPI_COMM_WORLD, &rank );

    for( ; mask < size; mask <<=1 ) {
        dst = (rank + mask) % size;
        src = (rank - mask + size) % size;
        MPI_Sendrecv( &tmp, 0, MPI_BYTE, dst, 1010,
                      &tmp, 0, MPI_BYTE, src, 1010,
                      MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
}

template <void (bfn)()>
void omp_aware_barrier()
{
#pragma omp barrier 
    bfn();   
#pragma omp barrier    
}

static void no_barrier()
{
}

typedef void (*barrier_func_t)();

struct input_benchmark_data {
    struct {
        int root;
    } collective;
    struct {
        int stride;
        bool anysource;
        bool anytag;
    } pt2pt;
    struct {
        int *cnt;
        int *displs;
    } collective_vector;
    struct {
        barrier_func_t fn_ptr;
    } barrier;
    struct {
        bool check;
    } checks;
};

struct output_benchmark_data {
    struct {
        double *time_ptr;
    } timing;
    struct {
        int failures;
    } checks;
};

typedef int (*mt_benchmark_func_t)(int repeat, int skip, void *in, void *out, int count,
                                   MPI_Datatype type, MPI_Comm comm, int ranks, int size, 
                                   input_benchmark_data *data, output_benchmark_data *odata);


template <typename T>
string out_field(T val);

template <int field_len, typename T>
string do_format(const char *fmt, T val) {
    char s[field_len+1];
    snprintf(s, field_len, fmt, val);
    s[field_len] = 0;
    return string(s);
}

template <> string out_field<double>(double val) { return do_format<14>("% 13.2f", val); }
template <> string out_field<int>(int val) { return do_format<14>("% 13d", val); }
template <> string out_field<const char *>(const char *val) { return do_format<14>("% 13s", val); }
template <> string out_field<unsigned long>(unsigned long val) { return do_format<14>("% 13ul", val); }

template <class bs, mt_benchmark_func_t fn_ptr>
class BenchmarkMT : public Benchmark {
    public:    
    enum Flags {
        COLLECTIVE,
        PT2PT,
        SEPARATE_MEASURING,
        COLLECTIVE_VECTOR,
        SEND_TO_ALL,
        RECV_FROM_ALL,
        SEND_TO_2,
        RECV_FROM_2,
        TIME_DIVIDE_BY_2,
        TIME_DIVIDE_BY_4,
        TIME_DIVIDE_BY_100,
        SCALE_BW_TWICE,
        SCALE_BW_FOUR,
        OUT_BYTES,
        OUT_REPEAT,
        OUT_TIME_MIN,
        OUT_TIME_MAX,
        OUT_TIME_AVG,
        OUT_BW,
        OUT_MSGRATE
    };
    std::set<Flags> flags; 
    MPI_Datatype datatype;
    size_t datatype_size;
    std::vector<void *> a;
    std::vector<void *> b;
    std::vector<input_benchmark_data *> idata;
    std::vector<output_benchmark_data *> odata;
    immb_local_t *input;
    int mode_multiple;
    int stride;
    int num_threads;
    barropt_t barrier_option;
    malopt_t malloc_option;
    int malloc_align;
    bool do_checks;
    double time_avg, time_min, time_max;
    int world_rank, world_size;
    public:
    virtual void init_flags();
    virtual void run_instance(immb_local_t *input, int count, double &t, int &result) {
        MPI_Comm comm = _ARRAY_THIS(input->comm);
        int warmup = input->warmup, repeat = input->repeat;
        if (repeat <= 0) return;
        int rank, size;
        MPI_Comm_rank(comm, &rank);
        MPI_Comm_size(comm, &size);
        void *in = a[omp_get_thread_num()];
        void *out = b[omp_get_thread_num()];
        input_benchmark_data &idata_local = *idata[omp_get_thread_num()];
        output_benchmark_data &odata_local = *odata[omp_get_thread_num()];
        idata_local.collective.root = 0;
        idata_local.pt2pt.stride = stride;
        
        idata_local.checks.check = do_checks;

        barrier_func_t bfn;
        switch (barrier_option) {
            case BARROPT_NOBARRIER: bfn = no_barrier; break;
            case BARROPT_NORMAL: 
                if (mode_multiple) {
                    bfn = omp_aware_barrier<normal_barrier>;
                } else {
                    bfn = normal_barrier;
                }
                break;
            case BARROPT_SPECIAL:
                if (mode_multiple) {
                    bfn = omp_aware_barrier<special_barrier>;
                } else {
                    bfn = special_barrier;
                }
                break;
            default: assert(0);
        }
        odata_local.checks.failures = 0;
        if (flags.count(SEPARATE_MEASURING)) {
            idata_local.barrier.fn_ptr = bfn;
            if (flags.count(COLLECTIVE_VECTOR)) {
                for (int i = 0; i < size; i++) {
                    idata_local.collective_vector.cnt[i] = count;
                    idata_local.collective_vector.displs[i] = count * i;
                }
            }
            odata_local.timing.time_ptr = &t;
            result = fn_ptr(repeat, warmup, in, out, count, datatype, comm, rank, size, &idata_local, &odata_local);
        } else {
            odata_local.timing.time_ptr = NULL;
            fn_ptr(warmup, 0, in, out, count, datatype, comm, rank, size, &idata_local, &odata_local);
            bfn();
            t = MPI_Wtime();
            result = fn_ptr(repeat, 0, in, out, count, datatype, comm, rank, size, &idata_local, &odata_local);
            t = MPI_Wtime()-t;
        }
        if (!result)
            t = 0;
            if (odata_local.checks.failures) {
                cout << "CHECK FAILURES: rank " << rank << ": " << odata_local.checks.failures << endl;
            }
        return;
    }
    virtual void init() {
        init_flags();
        GET_GLOBAL(int, mode_multiple);
        GET_GLOBAL(int, stride);
        GET_GLOBAL(int, num_threads);
        GET_GLOBAL(int, malloc_align);
        GET_GLOBAL(malopt_t, malloc_option);
        GET_GLOBAL(barropt_t, barrier_option);
        GET_GLOBAL(bool, do_checks);
        GET_GLOBAL(MPI_Datatype, datatype);
        int idts;
        MPI_Type_size(datatype, &idts);
        datatype_size = idts;
        input = (immb_local_t *)malloc(sizeof(immb_local_t) * num_threads);
        for (int thread_num = 0; thread_num < num_threads; thread_num++) {
            GET_GLOBAL_VEC(immb_local_t, input[thread_num], thread_num);
        }
        VarLenScope *sc = new VarLenScope(input[0].count, input[0].countn);
        scope = sc;

        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

        // get longest element from sequence
        size_t maxlen = sc->get_max_len();
        size_t size_a = datatype_size * maxlen;
        size_t size_b = datatype_size * maxlen;
        if (flags.count(SEND_TO_ALL))
            size_a *= world_size;
        else if (flags.count(SEND_TO_2))
            size_a *= 2;
        if (flags.count(RECV_FROM_ALL))
            size_b *= world_size;
        else if (flags.count(RECV_FROM_2))
            size_b *= 2;

        static AlignedAllocator<char> allocator(malloc_align);
        if (malloc_option == MALOPT_SERIAL) {
            for (int thread_num = 0; thread_num < num_threads; thread_num++) {
                a.push_back(allocator.Alloc(size_a));
                b.push_back(allocator.Alloc(size_b));
            }
        } else if (malloc_option == MALOPT_PARALLEL) {
            a.resize(num_threads);
            b.resize(num_threads);
#pragma omp parallel
            {
#pragma omp critical
                {
                    a[omp_get_thread_num()] = allocator.Alloc(size_a);
                    b[omp_get_thread_num()] = allocator.Alloc(size_b);
                }
            }
        } else if (malloc_option == MALOPT_CONTINOUS) {
            char *a_base = (char *)allocator.Alloc(size_a * num_threads);
            char *b_base = (char *)allocator.Alloc(size_b * num_threads);
            for (int thread_num = 0; thread_num < num_threads; thread_num++) {
                a.push_back(a_base + (size_t)thread_num * size_a);
                b.push_back(b_base + (size_t)thread_num * size_b);
            }
        }
        for (int thread_num = 0; thread_num < num_threads; thread_num++) {
            idata.push_back((input_benchmark_data *)malloc(sizeof(input_benchmark_data)));
            odata.push_back((output_benchmark_data *)malloc(sizeof(output_benchmark_data)));
            if (flags.count(COLLECTIVE_VECTOR)) {
                idata[idata.size()-1]->collective_vector.cnt = (int *)malloc(world_size * sizeof(int));
                idata[idata.size()-1]->collective_vector.displs = (int *)malloc(world_size * sizeof(int));
            }        
        }
  }
    virtual void run(const std::pair<int, int> &p) { 
        static int ninvocations = 0;
        double t, tavg = 0, tmin = 1e6, tmax = 0; 
        int nresults = 0;
        if (mode_multiple) {
        #pragma omp parallel default(shared)
            {
                double t_mp;
                int result;
                run_instance(&input[omp_get_thread_num()], p.second, t_mp, result);
            #pragma omp critical
                {
                    tmax = max(tmax, t_mp);
                    tmin = min(tmin, t_mp);
                    tavg = tavg + t_mp;
                    nresults += result;
                }
            }
        } else {
            run_instance(&input[0], p.second, t, nresults);
            tavg = tmax = tmin = t;
        }
        MPI_Allreduce(&tavg, &time_avg, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        MPI_Allreduce(&tmin, &time_min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
        MPI_Allreduce(&tmax, &time_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        MPI_Allreduce(MPI_IN_PLACE, &nresults, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        if (nresults) {
            time_avg /= (double)nresults;
        }
        time_avg /= (double)input[0].repeat;
        time_min /= (double)input[0].repeat;
        time_max /= (double)input[0].repeat;
        if (world_rank == 0) {
            double divider = 1.0, bw_multiplier = 1.0;
            if (flags.count(TIME_DIVIDE_BY_2)) divider *= 2.0;
            if (flags.count(TIME_DIVIDE_BY_4)) divider *= 4.0;
            if (flags.count(TIME_DIVIDE_BY_100)) divider *= 100.0;
            if (flags.count(SCALE_BW_TWICE)) bw_multiplier *= 2.0;
            if (flags.count(SCALE_BW_FOUR)) bw_multiplier *= 4.0;

            time_avg /= divider;
            time_min /= divider;
            time_max /= divider;
            if (nresults) {
                if (ninvocations++ == 0) {
                    cout << endl;
                    cout << "#-----------------------------------------------------------------------------" << endl;
                    cout << "# Benchmarking " << get_name() << endl;
                    cout << "# #processes = " << nresults / num_threads << " (threads: " << num_threads << ")" << endl;
                    cout << "#-----------------------------------------------------------------------------" << endl;
 
                    if (flags.count(OUT_BYTES)) cout << out_field("#bytes"); //"#bytes";
                    if (flags.count(OUT_REPEAT)) cout << out_field("#repetitions");
                    if (flags.count(OUT_TIME_MIN)) cout << out_field("t_min[usec]");
                    if (flags.count(OUT_TIME_MAX)) cout << out_field("t_max[usec]");
                    if (flags.count(OUT_TIME_AVG)) cout << out_field("t_avg[usec]");
                    if (flags.count(OUT_BW)) cout << out_field("Mbytes/sec");
                    if (flags.count(OUT_MSGRATE)) cout << out_field("Msg/sec");
                    cout << endl;
                }
                if (flags.count(OUT_BYTES)) cout << out_field(p.second * datatype_size);
                if (flags.count(OUT_REPEAT)) cout << out_field(input[0].repeat);
                if (flags.count(OUT_TIME_MIN)) cout << out_field(1e6 * time_min);
                if (flags.count(OUT_TIME_MAX)) cout << out_field(1e6 * time_max);
                if (flags.count(OUT_TIME_AVG)) cout << out_field(1e6 * time_avg);
                if (flags.count(OUT_BW)) cout << out_field((double)p.second * bw_multiplier / time_avg / 1e6);
                if (flags.count(OUT_MSGRATE)) cout << out_field((int)(1.0 / time_avg));
                cout << endl;
            }
            else {
                if (ninvocations++ == 0) {
                    cout << endl;
                    cout << "#-----------------------------------------------------------------------------" << endl;
                    cout << "# Benchmarking " << get_name() << endl;
                    cout << "# NO SUCCESSFUL EXECUTIONS" << endl;
                    cout << "#-----------------------------------------------------------------------------" << endl;
                    cout << endl;
                }
            }
        }
    }
    virtual void finalize() {
        free(input);
        for (int thread_num = 0; thread_num < num_threads; thread_num++) {
            if (flags.count(COLLECTIVE_VECTOR)) {
               free(idata[thread_num]->collective_vector.cnt);
               free(idata[thread_num]->collective_vector.displs);
            }
        }
    }
    DEFINE_INHERITED(GLUE_TYPENAME(BenchmarkMT<bs, fn_ptr>), bs);
};

