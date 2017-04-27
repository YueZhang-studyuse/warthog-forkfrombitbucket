#include "helpers.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <unistd.h>

bool
warthog::helpers::load_integer_labels(
        const char* filename, std::vector<uint32_t>& labels)
{
    std::ifstream ifs(filename, std::ios_base::in);
    if(!ifs.good())
    {
        std::cerr << "\nerror trying to load file " << filename << std::endl;
        ifs.close();
        return false;
    }

    while(true)
    {
        // skip comment lines
        while(ifs.peek() == '#' || ifs.peek() == 'c' || ifs.peek() == '%')
        {
            while(ifs.get() != '\n');
        }

        uint32_t tmp;
        ifs >> tmp;
        if(!ifs.good()) { break; }
        labels.push_back(tmp);
    }
    ifs.close();
    return true;
}

bool
warthog::helpers::load_integer_labels_dimacs(
        const char* filename, std::vector<uint32_t>& labels)
{
    // add a dummy to the front of the list if the labels are for use with
    // a dimacs graph. Such graphs use a 1-indexed scheme for node ids. we 
    // add the dummy so we can use the dimacs ids without conversion
    labels.push_back(0);
    return load_integer_labels(filename, labels);
}

void*
warthog::helpers::parallel_compute(void*(*fn_worker)(void*), 
        void* shared_data, uint32_t first_id, uint32_t last_id)
{
    std::cerr << "parallel compute; begin\n";
    std::cerr << "first " << first_id << " last " << last_id  << std::endl;

    // OK, let's fork some threads
    const uint32_t NUM_THREADS = 4;
    pthread_t threads[NUM_THREADS];
    thread_params task_data[NUM_THREADS];

    void*(*fn_task_wrapper)(void*) = [] (void* in) -> void*
    {
        thread_params* par = (thread_params*)in;
        par->thread_finished_ = false;
        void* retval = par->fn_worker_(in);
        par->thread_finished_ = true;
        return retval;
    };

    for(uint32_t i = 0; i < NUM_THREADS; i++)
    {
        // define workloads
        task_data[i].thread_id_ = i;
        task_data[i].max_threads_ = NUM_THREADS;
        task_data[i].nprocessed_ = 0;
        task_data[i].first_id_ = first_id;
        task_data[i].last_id_ = last_id;
        task_data[i].shared_ = shared_data;
        task_data[i].fn_worker_ = fn_worker;

        // gogogogo
        pthread_create(&threads[i], NULL, 
                fn_task_wrapper, (void*) &task_data[i]);
    }
    std::cerr << "forked " << NUM_THREADS << " threads \n";

    while(true)
    {
        // check progress
        uint32_t nprocessed = 0;
        uint32_t nfinished = 0;
        for(uint32_t i = 0; i < NUM_THREADS; i++)
        { 
            nprocessed += task_data[i].nprocessed_; 
            nfinished += task_data[i].thread_finished_;
        }

        std::cerr << "\rprogress: " << nprocessed << " / ";
        if(last_id == UINT32_MAX) { std::cerr << "?"; }
        else { std::cerr << last_id; }

        if(nfinished == NUM_THREADS) { break; }
        else { sleep(5); }
    }
    std::cerr << "\nparallel compute; end\n"<< std::endl;
    return 0;
    return 0;
}
