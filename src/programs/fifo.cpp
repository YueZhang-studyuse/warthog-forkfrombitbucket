// programs/fifo.cpp
//
// Run warthog reading from a FIFO (kernel-level file descriptor). This allows
// to interface with any other program able to output querysets to the FIFO.
//
// TODO There's a lot of duplicate code between here and 'programs/roadhog.cpp',
// mainly loading code. Find a way to DRY up?
//
#include <cstdlib>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <csignal>
#include <iostream>
#include <fstream>
#include <vector>
#include <omp.h>

#include "bch_expansion_policy.h"
#include "bch_search.h"
#include "cfg.h"
#include "ch_data.h"
#include "cpd_extractions.h"
#include "cpd_graph_expansion_policy.h"
#include "cpd_heuristic.h"
#include "cpd_search.h"
#include "graph_oracle.h"
#include "json.hpp"
#include "json_config.h"
#include "log.h"
#include "solution.h"
#include "timer.h"
#include "xy_graph.h"

using namespace std;

typedef std::function<void(warthog::search*, config&)> conf_fn;

// Defaults
std::string fifo = "/tmp/warthog.fifo";

//
// - Functions
//
void
signalHandler(int signum)
{
    warning(true, "Interrupt signal", signum, "received.");

    remove(fifo.c_str());

    exit(signum);
}

/**
 * The search function does a bunch of statistics out of the search. It takes a
 * configration object, an output pipe and a list of queries and processes them.
 */
void
run_search(vector<warthog::search*>& algos, conf_fn& apply_conf,
           config& conf, const std::string& fifo_out,
           const std::vector<t_query> &reqs, double t_read)
{
  assert(reqs.size() % 2 == 0);
  size_t n_results = reqs.size() / 2;
  // Statistics
  unsigned int n_expanded = 0;
  unsigned int n_inserted = 0;
  unsigned int n_touched = 0;
  unsigned int n_updated = 0;
  unsigned int n_surplus = 0;
  unsigned int plen = 0;
  unsigned int finished = 0;
  double t_astar = 0;

  warthog::timer t;
  user(conf.verbose, "Preparing to process", n_results, "queries using",
       (int)conf.threads, "threads.");

  t.start();

#pragma omp parallel num_threads(conf.threads)                          \
    reduction(+ : t_astar, n_expanded, n_inserted, n_touched, n_updated, \
              n_surplus, plen, finished)
    {
        // Parallel data
        const int thread_count = omp_get_num_threads();
        const int thread_id = omp_get_thread_num();

        warthog::timer t_thread;
        warthog::solution sol;
        warthog::search* alg = algos.at(thread_id);

        apply_conf(alg, conf);

        // Instead of bothering with manual conversiong (think 'ceil()'), we use
        // the magic of "usual arithmetic" to achieve the right from/to values.
        size_t step = n_results * thread_id, from = step / thread_count,
            to = (step + n_results) / thread_count;

        t_thread.start();
        // Iterate over the *requests* then convert to ids ({o,d} pair)
        for (auto id = from; id < to; id += 1)
        {
            size_t i = id * 2;
            warthog::sn_id_t start_id = reqs.at(i);
            warthog::sn_id_t target_id = reqs.at(i + 1);
            // Actual search
            warthog::problem_instance pi(start_id, target_id, conf.debug);
            alg->get_path(pi, sol);

            // Update stasts
            t_astar += sol.time_elapsed_nano_;
            n_expanded += sol.nodes_expanded_;
            n_inserted += sol.nodes_inserted_;
            n_touched += sol.nodes_touched_;
            n_updated += sol.nodes_updated_;
            n_surplus += sol.nodes_surplus_;
            plen += sol.path_.size();
            finished += sol.nodes_inserted_ > 0;
        }

        t_thread.stop();

#pragma omp critical
        trace(conf.verbose, "[", thread_id, "] Processed", to - from,
              "trips in", t_thread.elapsed_time_micro(), "us.");
    }

    t.stop();

    user(conf.verbose, "Processed", n_results, "in", t.elapsed_time_micro(),
         "us");

    std::streambuf* buf;
    std::ofstream of;
    if (fifo_out == "-")
    {
        buf = std::cout.rdbuf();
    }
    else
    {
        of.open(fifo_out);
        buf = of.rdbuf();
    }

    std::ostream out(buf);

    debug(conf.verbose, "Spawned a writer on", fifo_out);
    out << n_expanded << "," << n_inserted << "," << n_touched << ","
        << n_updated << "," << n_surplus << "," << plen << ","
        << finished << "," << t_astar << "," << t.elapsed_time_nano() + t_read
        << std::endl;

    if (fifo_out != "-") { of.close(); }
}

/**
 * The reader thread reads the data passed to the pipe ('FIFO') in the following
 * order:
 *
 *  1. the configuration for the search;
 *
 *  2. the output pipe's name and the number of queries; and,
 *
 *  3. the queries as (o, d)-pairs.
 *
 * It then passes the data to the search function before calling itself again.
 */
void
reader(vector<warthog::search*>& algos, conf_fn& apply_conf)
{
    ifstream fd;
    config conf;
    string fifo_out;
    vector<t_query> lines;
    warthog::timer t;

    while (true)
    {
        fd.open(fifo);
        debug(VERBOSE, "waiting for writers...");

        if (fd.good())
        {
            debug(VERBOSE, "Got a writer");
        }
        // else?
        t.start();

        // Start by reading config
        try
        {
            fd >> conf;
            sanitise_conf(conf);
        } // Ignore bad parsing and fall back on default conf
        catch (std::exception& e)
        {
            debug(conf.verbose, e.what());
        }

        trace(conf.verbose, conf);

        // Read output pipe and size of input
        size_t s = 0;
        fd >> fifo_out >> s;
        debug(conf.verbose, "Preparing to read", s, "items.");
        debug(conf.verbose, "Output to", fifo_out);

        warthog::sn_id_t o, d;
        size_t i = 0;

        lines.resize(s * 2);
        while (fd >> o >> d)
        {
            lines.at(i) = o;
            lines.at(i + 1) = d;
            i += 2;
        }
        fd.close();                 // TODO check if we need to keep this open
        t.stop();

        trace(conf.verbose, "Read", int(lines.size() / 2), "queries.");
        assert(lines.size() == s * 2);

        DO_ON_DEBUG_IF(conf.debug)
        {
            for (size_t i = 0; i < lines.size(); i += 2)
            {
                // Not using `verbose` here, it's a lot of info...
                debug(conf.debug, lines.at(i), ",", lines.at(i + 1));
            }
        }

        if (lines.size() > 0)
        {
            run_search(algos, apply_conf, conf, fifo_out, lines,
                       t.elapsed_time_nano());
        }
    }
}

void
run_cpd_search(warthog::util::cfg &cfg, warthog::graph::xy_graph &g,
               vector<warthog::search*> algos)
{
    std::ifstream ifs;
    // We first load the xy_graph and its diff as we need them to be *read* in
    // reverse order.
    std::string xy_filename = cfg.get_param_value("input");
    if(xy_filename == "")
    {
        std::cerr << "parameter is missing: --input [xy-graph file]\n";
        return;
    }

    ifs.open(xy_filename);
    if (!ifs.good())
    {
        std::cerr << "Could not open xy-graph: " << xy_filename << std::endl;
        return;
    }

    ifs >> g;
    ifs.close();

    // Check if we have a second parameter in the --input
    std::string diff_filename = cfg.get_param_value("input");
    if (diff_filename == "")
    {
        diff_filename = xy_filename + ".diff";
        ifs.open(diff_filename);
        if (!ifs.good())
        {
            std::cerr <<
                "Could not open diff-graph: " << diff_filename << std::endl;
            return;
        }

        g.perturb(ifs);
        ifs.close();
    }

    // read the cpd
    warthog::cpd::graph_oracle oracle(&g);
    std::string cpd_filename = cfg.get_param_value("input");
    if(cpd_filename == "")
    {
        cpd_filename = xy_filename + ".cpd";
    }

    ifs.open(cpd_filename);
    if(ifs.is_open())
    {
        ifs >> oracle;
        ifs.close();
    }
    else
    {
        std::cerr << "Could not find the CPD file." << std::endl;
        return;
    }

    for (auto& alg: algos)
    {
        warthog::simple_graph_expansion_policy* expander =
            new warthog::simple_graph_expansion_policy(&g);
        warthog::cpd_heuristic* h =
            new warthog::cpd_heuristic(&oracle, 1.0);
        warthog::pqueue_min* open = new warthog::pqueue_min();

        alg = new warthog::cpd_search<
            warthog::cpd_heuristic,
            warthog::simple_graph_expansion_policy,
            warthog::pqueue_min>(h, expander, open);
    }

    user(VERBOSE, "Loaded", algos.size(), "search.");

    conf_fn apply_conf = [] (warthog::search* base, config &conf) -> void
    {
        warthog::cpd_search<
            warthog::cpd_heuristic,
            warthog::simple_graph_expansion_policy,
            warthog::pqueue_min>* alg = static_cast<warthog::cpd_search<
                warthog::cpd_heuristic,
                warthog::simple_graph_expansion_policy,
                warthog::pqueue_min>*>(base);

        // Setup algo's config; we assume sane inputs
        alg->get_heuristic()->set_hscale(conf.hscale);
        alg->set_max_time_cutoff(conf.time); // This needs to be in ns
        alg->set_max_expansions_cutoff(conf.itrs);
        alg->set_max_k_moves(conf.k_moves);
        alg->set_quality_cutoff(conf.fscale);
    };

    reader(algos, apply_conf);
}

void
run_cpd(warthog::util::cfg &cfg, warthog::graph::xy_graph &g,
        vector<warthog::search*> algos)
{
    std::string xy_filename = cfg.get_param_value("input");
    if(xy_filename == "")
    {
        std::cerr << "parameter is missing: --input [xy-graph file]\n";
        return;
    }

    std::ifstream ifs(xy_filename);
    ifs >> g;
    ifs.close();

    warthog::cpd::graph_oracle oracle(&g);
    std::string cpd_filename = cfg.get_param_value("cpd");
    if(cpd_filename == "")
    {
        cpd_filename = xy_filename + ".cpd";
    }

    ifs.open(cpd_filename);
    if(ifs.is_open())
    {
        ifs >> oracle;
        ifs.close();
    }
    else
    {
        std::cerr << "Could not find CPD file." << std::endl;
        return;
    }

    for (auto& alg: algos)
    {
        alg = new warthog::cpd_extractions<warthog::cpd::graph_oracle>(
            &g, &oracle);
    }

    user(VERBOSE, "Loaded", algos.size(), "search.");

    conf_fn apply_conf = [] (warthog::search* base, config &conf) -> void
    {
        auto alg =
            static_cast<warthog::cpd_extractions<warthog::cpd::graph_oracle>*>(
                base);

        alg->set_max_k_moves(conf.k_moves);
    };

    reader(algos, apply_conf);
}

void
run_bch(warthog::util::cfg &cfg, warthog::graph::xy_graph &g,
        vector<warthog::search*> algos)
{
    std::string chd_file = cfg.get_param_value("input");
    if(chd_file == "")
    {
        std::cerr << "err; missing chd input file\n";
        return;
    }

    warthog::ch::ch_data chd;
    chd.type_ = warthog::ch::UP_ONLY;
    std::ifstream ifs(chd_file.c_str());
    if(!ifs.is_open())
    {
        std::cerr << "err; invalid path to chd input file\n";
        return;
    }

    ifs >> chd;
    ifs.close();

    for (auto& alg: algos)
    {
        warthog::bch_expansion_policy* fexp =
            new warthog::bch_expansion_policy(chd.g_);
        warthog::bch_expansion_policy* bexp =
            new warthog::bch_expansion_policy(chd.g_, true);
        warthog::zero_heuristic* h = new warthog::zero_heuristic();
        alg = new warthog::bch_search<
            warthog::zero_heuristic, warthog::bch_expansion_policy>
            (fexp, bexp, h);
    }

    user(VERBOSE, "Loaded", algos.size(), "search.");

    conf_fn apply_conf = [] (warthog::search* base, config &conf) -> void
    {};

    reader(algos, apply_conf);
}

/**
 * The main takes care of loading the data and spawning the reader thread.
 */
int
main(int argc, char *argv[])
{
	// parse arguments
	warthog::util::param valid_args[] =
        {
            // {"help", no_argument, &print_help, 1},
            // {"checkopt",  no_argument, &checkopt, 1},
            // {"verbose",  no_argument, &verbose, 1},
            // {"noheader",  no_argument, &suppress_header, 1},
            {"input", required_argument, 0, 1},
            {"fifo",  required_argument, 0, 1},
            {"alg",   required_argument, 0, 1},
            // {"problem",  required_argument, 0, 1},
            {0,  0, 0, 0}
        };

	warthog::util::cfg cfg;
    warthog::graph::xy_graph g;

	cfg.parse_args(argc, argv, "-f", valid_args);

    // TODO
    // if(argc == 1 || print_help)
    // {
	// 	help();
    //     exit(0);
    // }

    std::string alg_name = cfg.get_param_value("alg");
    if((alg_name == ""))
    {
        std::cerr << "parameter is missing: --alg\n";
        return EXIT_FAILURE;
    }

    vector<warthog::search*> algos;

#ifdef SINGLE_THREADED
    algos.resize(1);
#else
    algos.resize(omp_get_max_threads());
#endif

    std::string other = cfg.get_param_value("fifo");
    if (other != "")
    {
        fifo = other;
    }

    int status = mkfifo(fifo.c_str(), S_IFIFO | 0666);

    if (status < 0)
    {
        perror("mkfifo");
        return EXIT_FAILURE;
    }

    debug(true, "Reading from", fifo);

    // Register signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    if (alg_name == "cpd-search")
    {
        run_cpd_search(cfg, g, algos);
    }
    else if (alg_name == "cpd")
    {
        run_cpd(cfg, g, algos);
    }
    else if (alg_name == "bch")
    {
        run_bch(cfg, g, algos);
    }
    else
    {
        std::cerr << "--alg not recognised." << std::endl;
    }

    signalHandler(EXIT_FAILURE); // Is this even legal?

    // We do not exit from here
    return EXIT_FAILURE;
}
