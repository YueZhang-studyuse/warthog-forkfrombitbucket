#ifndef WARTHOG_FCH_DD_EXPANSION_POLICY
#define WARTHOG_FCH_DD_EXPANSION_POLICY

// contraction/fch_dd_expansion_policy.h
//
// An expansion policy for forward-driven
// search in contraction hiearchies combined 
// with a down-distance filtering scheme.
//
// @author: dharabor
// @created: 2016-08-02
//

#include "expansion_policy.h"
#include <vector>

namespace warthog
{

namespace graph
{
class planar_graph;
}

class problem_instance;
class search_node;
class euclidean_heuristic;
class down_distance_filter;

class fch_dd_expansion_policy : public expansion_policy
{
    public:
        fch_dd_expansion_policy(
                warthog::graph::planar_graph* graph,
                std::vector<uint32_t>* rank, 
                warthog::down_distance_filter* nf, 
                warthog::euclidean_heuristic* heuristic);

        ~fch_dd_expansion_policy();

		virtual void 
		expand(warthog::search_node*, warthog::problem_instance*);

        virtual void
        get_xy(uint32_t node_id, int32_t& x, int32_t& y);

        virtual warthog::search_node* 
        generate_start_node(warthog::problem_instance* pi);

        virtual warthog::search_node*
        generate_target_node(warthog::problem_instance* pi);

        virtual size_t
        mem()
        {
            return expansion_policy::mem() +
                sizeof(this);
        }

        inline void
        reset_metrics()
        {
            up_expansions_ = 0;
        }


    private:
        std::vector<uint32_t>* rank_;
        warthog::graph::planar_graph* g_;
        warthog::down_distance_filter* nf_;
        warthog::euclidean_heuristic* heuristic_;
        uint32_t up_expansions_;

        inline uint32_t
        get_rank(uint32_t id)
        {
            return rank_->at(id);
        }
};
}

#endif
