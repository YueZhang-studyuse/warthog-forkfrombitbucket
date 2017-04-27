#ifndef WARTHOG_LABEL_BBAF_LABELLING_H
#define WARTHOG_LABEL_BBAF_LABELLING_H

// label/bbaf_labelling.h
//
// Combined arcflags and bounding-box edge labelling.
//
// @author: dharabor
// @created: 2017-04-22
//

#include "flexible_astar.h"
#include "planar_graph.h"
#include "search_node.h"
#include "zero_heuristic.h"
#include "geom.h"
#include "helpers.h"

#include <algorithm>
#include <functional>
#include <unistd.h>

namespace warthog
{

namespace label
{

class bbaf_label
{
    public:
        bbaf_label()
        {
            flags_ = 0;
        }

        bbaf_label(const bbaf_label& other)
        {
            flags_ = other.flags_;
            bbox_ = other.bbox_;
        }

        uint8_t* flags_;
        warthog::geom::rectangle bbox_;
};

class bbaf_labelling
{
    public: 
        virtual ~bbaf_labelling();

        inline std::vector<uint32_t>*
        get_partitioning()
        {
            return part_;
        }

        inline warthog::graph::planar_graph*
        get_graph() 
        { 
            return g_; 
        }

        bbaf_label&
        get_label(uint32_t node_id, uint32_t edge_id)
        {
            return labels_.at(node_id).at(edge_id);
        }

        void
        print(std::ostream& out, 
                uint32_t firstid=0, 
                uint32_t lastid=UINT32_MAX);

        static warthog::label::bbaf_labelling*
        load_labels(const char* filename, warthog::graph::planar_graph* g, 
            std::vector<uint32_t>* partitioning);
        
        template <typename t_expander>
        static warthog::label::bbaf_labelling*
        compute(warthog::graph::planar_graph* g, std::vector<uint32_t>* part, 
                std::function<t_expander*(void)>& fn_new_expander,
                uint32_t first_id=0, uint32_t last_id=UINT32_MAX)
        {
            if(g == 0 || part  == 0) { return 0; } 
            std::cerr << "computing bbaf labels\n";

            // stuff that's shared between all worker threads
            struct bbaf_shared_data
            {
                std::function<t_expander*(void)> fn_new_expander_;
                warthog::label::bbaf_labelling* lab_;
            };

            void*(*thread_compute_fn)(void*) = [] (void* args_in) -> void*
            {
                warthog::helpers::thread_params* par = 
                    (warthog::helpers::thread_params*) args_in;
                bbaf_shared_data* shared = (bbaf_shared_data*)par->shared_;

                // need to keep track of the first edge on the way to the 
                // current node(the solution is a bit hacky as we break the 
                // chain of backpointers to achieve this; it doesn't matter, 
                // we don't care about the path)
                std::function<void(warthog::search_node*)> relax_fn = 
                [] (warthog::search_node* n) -> void
                {
                    // the start node and its children don't need their 
                    // parent pointers updated. for all other nodes the
                    // grandparent becomes the parent
                    if(n->get_parent()->get_parent() != 0)
                    {
                        if(n->get_parent()->get_parent()->get_parent() != 0)
                        {
                            n->set_parent(n->get_parent()->get_parent());
                        }
                    }
                };

                warthog::zero_heuristic heuristic;
                std::shared_ptr<t_expander> 
                    expander(shared->fn_new_expander_());

                warthog::flexible_astar<warthog::zero_heuristic, t_expander> 
                        dijkstra(&heuristic, expander.get());
                dijkstra.apply_on_relax(relax_fn);

                // sanity check the workload
                warthog::graph::planar_graph* g_ = shared->lab_->g_;
                uint32_t first_id = par->first_id_ > (g_->get_num_nodes()-1) ? 
                    0 : par->first_id_;
                uint32_t last_id = par->last_id_ > (g_->get_num_nodes()-1) ? 
                    g_->get_num_nodes() : par->last_id_;

                // run a dijkstra search from each node
                for(uint32_t i = first_id; i <= last_id; i++)
                {
                    // source nodes are evenly divided among all threads;
                    // skip any source nodes not intended for current thread
                    if((i % par->max_threads_) != par->thread_id_) 
                    { continue; }

                    // process the source node (i.e. run a dijkstra search)
                    uint32_t source_id = i;
                    uint32_t ext_source_id = g_->to_external_id(source_id);
                    warthog::problem_instance pi(ext_source_id, warthog::INF);
                    dijkstra.get_length(pi);

                    // now we analyse the closed list to compute arc flags
                    // but first, we need an easy way to convert between the 
                    // ids of nodes adjacent to the source and their 
                    // corresponding edge index
                    warthog::graph::node* source = g_->get_node(source_id);
                    std::unordered_map<uint32_t, uint32_t> idmap;
                    uint32_t edge_idx = 0;
                    for(warthog::graph::edge_iter it = 
                            source->outgoing_begin(); 
                            it != source->outgoing_end(); it++)
                    {
                        idmap.insert(
                            std::pair<uint32_t, uint32_t>
                                ((*it).node_id_, edge_idx));
                        edge_idx++;
                    }

                    // now analyse the nodes on the closed list and label the 
                    // edges of the source node accordingly
                    std::function<void(warthog::search_node*)> fn_arcflags =
                    [shared, &source_id, &idmap](warthog::search_node* n) 
                    -> void
                    {
                        // skip edges from the source to itself
                        assert(n);
                        if(n->get_id() == source_id) { return; } 
                        assert(n->get_parent());

                        // label the edges of the source
                        // (TODO: make this stuff faster)
                        uint32_t part_id 
                            = shared->lab_->part_->at(n->get_id());
                        uint32_t e_idx  
                            = (*idmap.find(
                                n->get_parent()->get_id() == source_id ? 
                                n->get_id() : 
                                n->get_parent()->get_id())).second;
                        shared->lab_->labels_.at(source_id).at(
                                e_idx).flags_[part_id >> 3] |= 
                                    (1 << (part_id & 7));

                        int32_t x, y;
                        shared->lab_->g_->get_xy(n->get_id(), x, y);
                        assert(x != warthog::INF && y != warthog::INF);
                        shared->lab_->labels_.at(source_id).at(
                                e_idx).bbox_.grow(x, y);
                        assert(
                            shared->lab_->labels_.at(source_id).at(
                                e_idx).bbox_.is_valid());
                    };
                    dijkstra.apply_to_closed(fn_arcflags);
                    par->nprocessed_++;
                }
                return 0;
            };

            warthog::label::bbaf_labelling* lab = 
                new warthog::label::bbaf_labelling(g, part);
            bbaf_shared_data shared;
            shared.lab_ = lab;
            shared.fn_new_expander_ = fn_new_expander;
            warthog::helpers::parallel_compute(thread_compute_fn, &shared, 
                    first_id, last_id);
            return lab;
        }

    private:
        bbaf_labelling(
            warthog::graph::planar_graph* g, 
            std::vector<uint32_t>* partitioning);

        warthog::graph::planar_graph* g_;
        std::vector<uint32_t>* part_;

        std::vector<std::vector<bbaf_label>> labels_;
        uint32_t bytes_per_af_label_;
};

} // warthog::label::

} // warthog::

#endif