#ifndef WARTHOG_DOWN_DISTANCE_FILTER_H
#define WARTHOG_DOWN_DISTANCE_FILTER_H

#include <vector>
#include <cstdint>

namespace warthog
{

namespace graph
{

class planar_graph;

} 

class search_node;
class down_distance_filter 
{
    public:
        down_distance_filter(
                const char* ddfile, 
                warthog::graph::planar_graph* g);

        down_distance_filter(
                warthog::graph::planar_graph* g);

        ~down_distance_filter();

        bool 
        filter(warthog::search_node* n, uint32_t succ_num);

        double
        get_down_distance(uint32_t);

        void
        print(std::ostream& out);

        void
        compute(uint32_t startid, uint32_t endid, 
                std::vector<uint32_t>* rank);
                
        void
        compute(std::vector<uint32_t>* rank);

        bool
        load_labels(const char* filename);

    private:
        std::vector<double>* ddist_;
        warthog::graph::planar_graph* g_;
        uint32_t start_id_, last_id_;

};

}

#endif
