#include "gridmap.h"
#include "xy_graph.h"
#include "scenario_manager.h"

#include <cstring>
#include <errno.h>
#include <iostream>
#include <memory>

void
help()
{
    std::cerr 
       << "Converts from the format used at the Grid-based Path Planning Competition "
       << "\nand the xy_graph format used by the Warthog Pathfinding Library\n"
       << "\n"
       << "Usage: ./grid2graph [map | scen] [grid file]"
       << "\n\nParameter descriptions: " 
       << "\n\tmap: convert directly from a grid map to an xy_graph"
       << "\n\tscen: convert a gridmap scenario file into an xy_graph problem file\n";

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
        warthog::gridmap gm(argv[2]);
        warthog::graph::xy_graph g;
        warthog::graph::gridmap_to_xy_graph(&gm, &g);
        std::cout << g;
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

        // we need to convert (x, y) coordinates into graph ids
        // so we load up the associated grid and map ids
        warthog::gridmap gm(scenmgr.get_experiment(0)->map().c_str());
        warthog::gridmap_expansion_policy exp(&gm);

        // xy graph ids are assigned by 
        std::vector<uint32_t> id_map(gm.header_height() * gm.header_width());
        uint32_t next_graph_id = 0;
        for(uint32_t y = 0; y < gm.header_height(); y++)
        {
            for(uint32_t x = 0; x < gm.header_width(); x++)
            {
                // skip obstacles
                uint32_t from_gm_id = y * gm.header_width() + x;
                if(!gm.get_label(gm.to_padded_id(from_gm_id))) 
                { continue; }

                // add graph node (we scale up all costs and coordinates)
                id_map[from_gm_id] = next_graph_id++;
            }
        }

        std::cout 
            << "c Zero-indexed point-to-point problem instances, converted from the gridmap scenario file\n"
            << "c " << argv[2] << std::endl
            << "c Each point identifies a traversable grid tile and the ids are generated by\n"
            << "c scanning the associated grid map left-to-right and top-to-bottom\n"
            << std::endl;

        std::cout
            << "p aux sp p2p-zero " << scenmgr.num_experiments() << std::endl;

        for(uint32_t i = 0; i < scenmgr.num_experiments(); i++)
        {
            warthog::experiment* exp = scenmgr.get_experiment(i);
            uint32_t start_id = 
                exp->starty() * exp->mapwidth() + exp->startx();
            uint32_t goal_id  = 
                exp->goaly() * exp->mapwidth() + exp->goalx();
            std::cout << "q " << id_map[start_id] << " " << id_map[goal_id] << std::endl;
        }
    }
    else
    {
        std::cerr << "err; must specify type of conversion and file\n";
        return EINVAL;
    }
    return 0;
}

