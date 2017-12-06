#include "graph_contraction.h"
#include "apriori_filter.h"
#include "planar_graph.h"

warthog::ch::graph_contraction::graph_contraction(
        warthog::graph::planar_graph* g) 
{
    g_ = g;
    //filter_ = new warthog::apriori_filter(g_->get_num_nodes());
    done_ = false;
    verbose_ = false;
    c_pct_ = 100;
}

warthog::ch::graph_contraction::~graph_contraction()
{
    //delete filter_;
}

void
warthog::ch::graph_contraction::contract()
{
    if(done_) { return; }
    done_ = true;

    if(c_pct_ < 100)
    {
        std::cerr << "partially "
                  << "("<<c_pct_<<"% of nodes) ";
    }
    std::cerr << "contracting graph " << g_->get_filename() << std::endl;
    uint32_t edges_before = g_->get_num_edges();

    preliminaries();

    uint32_t total_nodes = g_->get_num_nodes();
    uint32_t num_contractions = 0;
//    warthog::apriori_filter* filter = get_filter();
    for(uint32_t cid = next(); cid != warthog::INF; cid = next())
    {
        
        uint32_t pct = (num_contractions / (double)g_->get_num_nodes()) * 100;
        if(pct >= c_pct_)
        { 
            std::cerr << "\npartial contraction finished " 
                      << "(processed "<< pct << "% of all nodes)";
            break; 
        }

        std::cerr << "\r " << pct << "%; " << ++num_contractions << " /  " << total_nodes;
        if(verbose_)
        {
            std::cerr << "; current: " << cid;
        }

        warthog::graph::node* n = g_->get_node(cid);
        //filter->filter(cid); // never expand this node again

        for(int i = 0; i < n->out_degree(); i++)
        {
            warthog::graph::edge& out = *(n->outgoing_begin() + i);

            // skip already-contracted neighbours
//            if(filter->filter(out.node_id_)) { continue; }

            for(int j = 0; j < n->in_degree(); j++)
            {
                warthog::graph::edge& in = *(n->incoming_begin() + j);

                // skip already-contracted neighbours
//                if(filter->filter(in.node_id_)) { continue; }

                // no reflexive arcs please
                if(out.node_id_ == in.node_id_) { continue; }

                // terminate when we prove there is no witness 
                // path with len <= via_len
                double via_len = in.wt_ + out.wt_;
                double witness_len = 
                    witness_search(in.node_id_, out.node_id_, via_len);

                if(witness_len > via_len)
                {
                    if(verbose_)
                    {
                        std::cerr << "\tshortcut " << in.node_id_ << " -> "
                            << cid << " -> " << out.node_id_;
                        std::cerr << " via-len " << via_len;
                        std::cerr << " witness-len " << witness_len << std::endl;
                    }

                    warthog::graph::node* tail = g_->get_node(in.node_id_);
                    tail->add_outgoing(
                            warthog::graph::edge(out.node_id_, via_len));
                    warthog::graph::node* head = g_->get_node(out.node_id_);
                    head->add_incoming(
                            warthog::graph::edge(in.node_id_, via_len));
                }
            }
        }
    }

    std::cerr << "\ngraph, contracted. ";
    std::cerr << "edges before " << edges_before 
        << "; edges after " << g_->get_num_edges() << std::endl;
    postliminaries();
}

size_t
warthog::ch::graph_contraction::mem()
{
    return 
       // filter_->mem() + 
        sizeof(this);
}