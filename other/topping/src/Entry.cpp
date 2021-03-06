#include <deque>
#include <vector>
#include <algorithm>
#include <assert.h>
#include "Entry.h"

#include "Timer.h"
#include "list_graph.h"
#include "mapper.h"
#include "cpd.h"
#include "order.h"
#include "adj_graph.h"
#include "dijkstra.h"
#include "balanced_min_cut.h"
#include "prefer_zero_cut.h"
#include <cstdio>

using namespace std;


#ifdef USE_PARALLELISM
#include <omp.h>
#endif

const char *GetName()
{
	#ifndef USE_CUT_ORDER
	return "DFS-SRC-RLE";
	#else
	return "METIS-CUT-SRC-RLE";
	#endif
}

void PreprocessMap(std::vector<bool> &bits, int width, int height, const char *filename)
{
	Mapper mapper(bits, width, height);
	printf("width = %d, height = %d, node_count = %d\n", width, height, mapper.node_count());

	printf("Computing node order\n");
	#ifndef USE_CUT_ORDER
	NodeOrdering order = compute_real_dfs_order(extract_graph(mapper));
	#else
	NodeOrdering order = compute_cut_order(extract_graph(mapper), prefer_zero_cut(balanced_min_cut));	
	#endif
	mapper.reorder(order);


	printf("Computing first-move matrix\n");

	CPD cpd;
	{
		AdjGraph g(extract_graph(mapper));
		
		{
			Dijkstra dij(g);
			Timer t;
			t.StartTimer();
			dij.run(0);
			t.EndTimer();
			printf("Estimated sequential running time : %dmin\n", static_cast<int>(t.GetElapsedTime()*g.node_count()/60.0));
		}

		#ifndef USE_PARALLELISM
		Dijkstra dij(g);
		for(int source_node=0; source_node < g.node_count(); ++source_node){
			if(source_node % (g.node_count()/10) == 0)
				printf("%d of %d done\n", source_node, g.node_count());

			const auto&allowed = dij.run(source_node);
			cpd.append_row(source_node, allowed);
		}
		#else
		printf("Using %d threads\n", omp_get_max_threads());
		vector<CPD>thread_cpd(omp_get_max_threads());

		int progress = 0;

		#pragma omp parallel
		{
			const int thread_count = omp_get_num_threads();
			const int thread_id = omp_get_thread_num();
			const int node_count = g.node_count();

			int node_begin = (node_count*thread_id) / thread_count;
			int node_end = (node_count*(thread_id+1)) / thread_count;

			AdjGraph thread_adj_g(g);
			Dijkstra thread_dij(thread_adj_g);
			for(int source_node=node_begin; source_node < node_end; ++source_node){
				thread_cpd[thread_id].append_row(source_node, thread_dij.run(source_node));
				#pragma omp critical 
				{
					++progress;
					if(progress % (g.node_count()/10) == 0){
						printf("%d of %d done\n", progress, g.node_count());
						fflush(stdout);
					}
				}
			}
		}

		for(auto&x:thread_cpd)
			cpd.append_rows(x);
		#endif
	}

	printf("Saving data to %s\n", filename);
	FILE*f = fopen(filename, "wb");
	order.save(f);
	cpd.save(f);
	fclose(f);
	printf("Done\n");

}

struct State{
	CPD cpd;
	Mapper mapper;
	AdjGraph graph;
	int current_node;
	int target_node;
};

void *PrepareForSearch(std::vector<bool> &bits, int w, int h, const char *filename)
{
	printf("Loading preprocessing data\n");
	State*state = new State;
	
	state->mapper = Mapper(bits, w, h);

	FILE*f = fopen(filename, "rb");
	NodeOrdering order;
	order.load(f);
	state->cpd.load(f);
	fclose(f);

	state->mapper.reorder(order);

	state->graph = AdjGraph(extract_graph(state->mapper));
	state->current_node = -1;

	printf("Loading done\n");


	return state;
}

std::int16_t dx[] = {0, 0, 1, -1, 1, -1, 1, -1};
std::int16_t dy[] = {-1, 1, 0, 0, -1, -1, 1, +1};

void GetPath(void *data, xyLoc s, xyLoc t, std::vector<xyLoc> &path, warthog::jpsp_oracle& oracle)//, int &callCPD)
{
	State*state = static_cast<State*>(data);
	int current_source = state->mapper(s);
	int current_target = state->mapper(t);

	unsigned char move = state->cpd.get_first_move(current_source, current_target);

	if(move != 0xF && current_source != current_target){
//		callCPD++;
		oracle.set_goal_location(t.x,t.y);
		warthog::jps::direction direction = (warthog::jps::direction)(1 << move);
		int number_step_to_turn = oracle.next_jump_point(s.x, s.y, direction);

		path.push_back(s);

		for(;;){

			for(int i = 0; i < number_step_to_turn; i++){
				s.x += dx[move];
				s.y += dy[move];
				current_source = state->mapper(s);
				path.push_back(s);
				if(current_source == current_target)
					break;
			}

			if(current_source == current_target)
				break;
//			callCPD++;

			move = state->cpd.get_first_move(current_source, current_target);
			direction = (warthog::jps::direction)(1 << move);
			number_step_to_turn = oracle.next_jump_point(s.x, s.y, direction);

		}
	}
}



