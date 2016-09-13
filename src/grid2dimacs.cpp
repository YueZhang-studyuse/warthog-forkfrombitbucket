#include "planar_graph.h"
#include "scenario_manager.h"

#include <cstring>
#include <errno.h>
#include <iostream>

void
help()
{
    std::cerr 
       << "Converts between the graph and experiment formats used at the \n"
       << "Grid-based Path Planning Competition and the format used at \n"
       << "the 9th DIMACS Implementation Challenge\n"
       << "Usage: ./grid2dimacs [map | scen] [grid file]\n";
}

int 
main(int argc, char** argv)
{
    if(argc != 3)
    {
		help();
        exit(0);
    }

    if(strcmp(argv[1], "map") == 0)
    {
        warthog::graph::planar_graph g;
        g.load_grid(argv[2]);
        g.print_dimacs_gr(std::cout);
        g.print_dimacs_co(std::cout);
    }
    else if(strcmp(argv[1], "scen") == 0)
    {
        warthog::scenario_manager scenmgr;
        scenmgr.load_scenario(argv[2]);
        if(scenmgr.num_experiments() == 0)
        {
            std::cerr << "warning: scenario file contains no experiments\n";
            return 0;
        }
        std::string map = scenmgr.get_experiment(0)->map();
        std::cout 
            << "c Scenarios for gridmap file\n"
            << "c " << map << std::endl;
        std::cout
            << "p aux sp p2p " << scenmgr.num_experiments() << std::endl;

        for(uint32_t i = 0; i < scenmgr.num_experiments(); i++)
        {
            warthog::experiment* exp = scenmgr.get_experiment(i);
            uint32_t start_id = 
                exp->starty() * exp->mapwidth() + exp->startx();
            uint32_t goal_id  = 
                exp->goaly() * exp->mapwidth() + exp->goalx();
            std::cout << "q " << start_id << " " << goal_id << std::endl;
        }
    }
    else
    {
        std::cerr << "err; must specify type of conversion and file\n";
        return EINVAL;
    }
    return 0;
}

