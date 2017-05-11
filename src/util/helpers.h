#ifndef WARTHOG_HELPERS_H
#define WARTHOG_HELPERS_H

// helpers.h
//
// Helper functions that don't fit anywhere else.
//
// @author: dharabor
// @created: 21/08/2012
//

#include <vector>
#include <cstdlib>

namespace warthog
{

namespace helpers
{

// convert id into x/y coordinates on a grid of width 'mapwidth'
inline void
index_to_xy(unsigned int id, unsigned int mapwidth, 
		int32_t& x, int32_t& y)
{	
	y = id / mapwidth;
	x = id % mapwidth;
}

// sometimes the nodes of the search graph need to be labeled with
// integer data (e.g. during a partitioning of the graph)
// this file loads up such integer labels from a file. It assumes each
// line of the file contains a single integer.
// Comments lines can exist in the file; these need to begin with one
// of the following characters: #, %, c
bool
load_integer_labels(const char* filename, std::vector<uint32_t>& labels);

bool
load_integer_labels_dimacs(const char* filename, 
        std::vector<uint32_t>& labels);

struct thread_params
{
    // thread data
    uint32_t thread_id_;
    uint32_t max_threads_;
    bool thread_finished_;
    void*(*fn_worker_)(void*);

    // task data
    uint32_t nprocessed_;
    uint32_t first_id_;
    uint32_t last_id_;
    void* shared_;
};

// helper code for simple parallel computations.
// simple in this case means no synchronisation between threads.
// @param fn_worker: the actual precompute function:
//          - it takes as input a pointer whose actual type is
//          warthog::label::thread_params
//          - it returns a (possibly null) pointer to a result 
// @param shared_data: 
//         a pointer to data which will be shared among all worker 
//         threads
// @param first_id: start of the workload (typically a graph node id)
// @param last_id: end of the workload (typically a graph node id)
//
// @return: 0 (the function always succeeds)
void*
parallel_compute(void*(*fn_worker)(void*), void* shared_data, 
        uint32_t first_id=0, uint32_t last_id=UINT32_MAX);

}
}

#endif

