/**
 * This file is used to create CPDs in an independent fashion.
 */
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <omp.h>

#include "cfg.h"
#include "graph_oracle.h"
#include "log.h"
#include "xy_graph.h"

using namespace std;

int
make_cpd(std::string xy_filename, int from, int to, bool verbose=false)
{
    warthog::graph::xy_graph g;
    std::ifstream ifs(xy_filename);

    if (!ifs.good())
    {
        std::cerr << "Cannot open file " << xy_filename << std::endl;
        return 1;
    }

    ifs >> g;
    ifs.close();

    // This needs to be done after loading the graph
    warthog::cpd::graph_oracle cpd(&g);
    uint32_t node_count = g.get_num_nodes();
    if (to < 0)
    {
        to = node_count;
    }

    assert(to > 0);
    assert((unsigned int)from < node_count);
    uint32_t node_end = to;
    unsigned char pct_done = 0;
    uint32_t nprocessed = 0;

    warthog::timer t;
    t.start();

    info(verbose, "Computing node ordering.");
    cpd.compute_dfs_preorder();

    info(verbose, "Computing Dijkstra labels.");
    std::cerr << "progress: [";
    for(uint32_t i = 0; i < 100; i++) { std::cerr <<" "; }
    std::cerr << "]\rprogress: [";

#pragma omp parallel
    {
        int thread_count = omp_get_num_threads();
        int thread_id = omp_get_thread_num();

        // warthog ids are 0-indexed, so no need to do anything.
        warthog::sn_id_t source_id = from + thread_id;

        std::vector<warthog::cpd::fm_coll> s_row(node_count);
        // each thread has its own copy of Dijkstra and each
        // copy has a separate memory pool
        warthog::simple_graph_expansion_policy expander(&g);
        warthog::zero_heuristic h;
        warthog::pqueue_min queue;
        warthog::cpd::graph_oracle_listener listener;

        warthog::flexible_astar<
            warthog::zero_heuristic,
            warthog::simple_graph_expansion_policy,
            warthog::pqueue_min,
            warthog::cpd::graph_oracle_listener>
                dijk(&h, &expander, &queue, &listener);

        listener.oracle_ = &cpd;
        listener.source_id_ = &source_id;
        listener.s_row_ = &s_row;
        dijk.set_listener(&listener);

        while (source_id < node_end)
        {
            warthog::cpd::compute_row(source_id, &cpd, &dijk, s_row);
            // No idea why we want to play with an int pointer...
            //
            // We increment the source by the number of threads to *jump* to
            // that id.
            source_id += thread_count;
            #pragma omp critical
            {
                nprocessed++;

                if ((nprocessed * 100 / (to - from)) > pct_done)
                {
                    std::cerr << "=";
                    pct_done++;
                }
            }
        }
    }

    std::cerr << std::endl;
    // convert the column order into a map: from vertex id to its ordered index
    cpd.value_index_swap_array();

    t.stop();
    info(verbose, "total preproc time (seconds):", t.elapsed_time_sec());

    std::string cpd_filename = xy_filename + ".cpd";
    std::ofstream ofs(cpd_filename);

    if (!ofs.good())
    {
        std::cerr << "Could not open CPD file " << cpd_filename << std::endl;
        return 1;
    }

    info(verbose, "Writing results to", cpd_filename);
    ofs << cpd;
    ofs.close();

    return 0;
}

int
main(int argc, char *argv[])
{
    int verbose = 0;
    warthog::util::param valid_args[] =
    {
        {"from", required_argument, 0, 1},
        {"to", required_argument, 0, 1},
        {"input", required_argument, 0, 1},
        {"verbose", no_argument, &verbose, 1},
        {0, 0, 0, 0}
    };

    warthog::util::cfg cfg;
    cfg.parse_args(argc, argv, valid_args);

    std::string s_from = cfg.get_param_value("from");
    std::string s_to = cfg.get_param_value("to");
    std::string fname = cfg.get_param_value("input");

    if (fname == "")
    {
        std::cerr << "Required argument --input missing." << std::endl;
        return 1;
    }

    int from = 0;
    int to = -1;

    if (s_from != "")
    {
        from = std::stoi(s_from);

        if (from < 0)
        {
            std::cerr << "Argument --from cannot be negative." << std::endl;
        }
    }

    if (s_to != "")
    {
        to = std::stoi(s_to);
    }

    return make_cpd(fname, from, to, verbose);
}