#include "constants.h"
#include "dimacs_parser.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ostream>
#include <set>
#include <string>
#include <vector>
#include <unordered_map>

warthog::dimacs_parser::dimacs_parser()
{
    init();
}

warthog::dimacs_parser::dimacs_parser(const char* gr_file)
{
    init();
    load_graph(gr_file);
}

warthog::dimacs_parser::dimacs_parser(const char* co_file, 
        const char* gr_file)
{
    init();
    load_graph(co_file);
    load_graph(gr_file);
}

warthog::dimacs_parser::~dimacs_parser()
{
    delete nodes_;
    delete edges_;
    delete experiments_;
}

void
warthog::dimacs_parser::init()
{
    nodes_ = new std::vector<warthog::dimacs_parser::node>();
    edges_ = new std::vector<warthog::dimacs_parser::edge>();
    experiments_ = new std::vector<warthog::dimacs_parser::experiment>();
}

bool
warthog::dimacs_parser::load_graph(const char* filename)
{
    std::fstream* fdimacs = new std::fstream(filename, std::fstream::in);
	if(!fdimacs->is_open())
	{
		std::cerr << "err; dimacs_parser::dimacs_parser "
			"cannot open file: "<<filename << std::endl;
		return false;
	}

	bool retval = false;
	char* buf = new char[1024];
	const char* delim = " \t";
	uint32_t line = 0;
	while(fdimacs->good())
	{
		fdimacs->getline(buf, 1024);
		if(buf[0] == 'p')
		{
			char* type = strtok(buf, delim); // p char
			type = strtok(NULL, delim);
			if(!strcmp(type, "sp"))
			{
				retval |= load_gr_file(*fdimacs);
			}
			else if(!strcmp(type, "aux"))
			{
				retval |= load_co_file(*fdimacs);
			}
			else
			{
				std::cerr << "error; unrecognised problem line in dimacs file\n";
				break;
			}
		}
		line++;
	}

    delete fdimacs;
	delete [] buf;
    return retval;
}

bool
warthog::dimacs_parser::load_co_file(std::istream& fdimacs)
{
    nodes_->clear();
    uint32_t line = 1;
	char* buf = new char[1024];
	const char* delim = " \t";
	bool early_abort = false;
	while(fdimacs.good() && !early_abort)
	{
		char next_char = fdimacs.peek();
		switch(next_char)
		{
			case 'v':
			{
				fdimacs.getline(buf, 1024);
				char* descriptor = strtok(buf, delim);
				char* id = strtok(NULL, delim);
				char* x = strtok(NULL, delim);
				char* y = strtok(NULL, delim);
				if(!(descriptor && id && x && y))
				{
					std::cerr << "warning; badly formatted node descriptor on line "
                        <<line << std::endl;
					break;
				}
                warthog::dimacs_parser::node n;
                n.id_ = atoi(id);
                n.x_ = atoi(x);
                n.y_ = atoi(y);
                nodes_->push_back(n);

				break;
			}
			case 'p': // stop if we hit another problem line
				early_abort = true;
				break;
			default: // ignore non-node, non-problem lines
				fdimacs.getline(buf, 1024);
				break;
		}
		line++;
	}

	delete [] buf;
	return !early_abort;
}

bool
warthog::dimacs_parser::load_gr_file(std::istream& fdimacs)
{
    edges_->clear();

    uint32_t line = 1;
	char* buf = new char[1024];
	const char* delim = " \t";
	bool early_abort = false;
	while(fdimacs.good() && !early_abort)
	{
		char next_char = fdimacs.peek();
		switch(next_char)
		{
			case 'a':
			{
				fdimacs.getline(buf, 1024);
				char* descriptor = strtok(buf, delim);
				char* from = strtok(NULL, delim);
				char* to = strtok(NULL, delim);
				char* cost = strtok(NULL, delim);
				if(!(descriptor && from && to && cost))
				{
					std::cerr << "warning; badly formatted arc descriptor on line "
                        <<line << std::endl;
					break;
				}
                warthog::dimacs_parser::edge e;
                e.tail_id_ = atoi(from);
                e.head_id_ = atoi(to);
                e.weight_ = atoi(cost);
                edges_->push_back(e);
				break;
			}
			case 'p': // another problem line. stop here
				early_abort = true;
				break;
			default: // ignore non-arc, non-problem lines
				fdimacs.getline(buf, 1024);
				break;
		}
		line++;
	}

	delete [] buf;
	return !early_abort;
}

void
warthog::dimacs_parser::print(std::ostream& oss)
{
    uint32_t nnodes = nodes_->size();
    if(nnodes > 0)
    {
        oss << "p aux sp co " << nodes_->size() << std::endl;
        for(uint32_t i = 0; i < nnodes; i++)
        {
            warthog::dimacs_parser::node n = nodes_->at(i);
            oss << "v " << i+1 << " " << n.x_ << " " << n.y_ << std::endl;
        }
    }

    uint32_t nedges = edges_->size();
    if(nedges > 0)
    {
        oss << "p sp " << nnodes << " " << nedges << std::endl;
        for(uint32_t i = 0; i < nedges; i++)
        {
            warthog::dimacs_parser::edge e = edges_->at(i);
            oss << "a " << e.tail_id_ << " " << e.head_id_ << " " << e.weight_ << std::endl;
        }
    }

}

bool
warthog::dimacs_parser::load_instance(const char* dimacs_file)
{
    problemfile_ = std::string(dimacs_file);
	std::ifstream infile;
	infile.open(dimacs_file,std::ios::in);


    bool p2p = true;
    char buf[1024];
    while(infile.good())
    {
        infile.getline(buf, 1024);
        // skip comment lines
        if(buf[0] == 'c')
        {
            continue;
        }
        if(strstr(buf, "p aux sp p2p") != 0)
        {
            p2p = true;
            break;
        }
        else if(strstr(buf, "p aux sp ss") != 0)
        {
            p2p = false;
            break;
        }
    }
        
    infile.getline(buf, 1024);
    while(infile.good())
    {
        if(buf[0] == 'c')
        {
            infile.getline(buf, 1024);
            continue;
        }

        char* tok = strtok(buf, " \t\n");
        if(strcmp(tok, "q") == 0)
        {
            warthog::dimacs_parser::experiment exp;

            tok = strtok(0, " \t\n");
            if(tok == 0)
            {
                std::cerr << "skipping invalid query in problem file:  " 
                    << buf << "\n";
                continue;

            }
            exp.source = atoi(tok);
            exp.p2p = p2p;

            if(p2p)
            {
                tok = strtok(0, " \t\n");
                if(tok == 0)
                {
                    std::cerr << "invalid query in problem file:  " << buf << "\n";
                }
                exp.target = atoi(tok);
            }
            else
            {
                exp.target = warthog::INF;
            }
            experiments_->push_back(exp);
        } 
        else
        {
            std::cerr << "skipping invalid query in problem file: " 
                << buf << std::endl;
        }
        infile.getline(buf, 1024);
    }
    //std::cerr << "loaded "<<experiments_->size() << " queries\n";
    return true;
}

void
warthog::dimacs_parser::print_undirected_unweighted_metis(std::ostream& out)
{
    std::unordered_map<uint32_t, std::set<uint32_t>> adj;
    std::set<uint32_t> nodes;

    uint32_t num_undirected_edges = 0;

    // enumerate all the nodes
    for(uint32_t i = 0; i < edges_->size(); i++)
    {
        warthog::dimacs_parser::edge e = edges_->at(i);
        uint32_t id1 = e.head_id_;
        uint32_t id2 = e.tail_id_;
        nodes.insert(id1);
        nodes.insert(id2);

        if(adj.find(id1) == adj.end())
        {
            std::set<uint32_t> neis;
            std::pair<uint32_t, std::set<uint32_t>> elt(id1, neis);
            adj.insert(elt);
        }
        if(adj.find(id2) == adj.end())
        {
            std::set<uint32_t> neis;
            std::pair<uint32_t, std::set<uint32_t>> elt(id2, neis);
            adj.insert(elt);
        }
        std::set<uint32_t>& neis1 = adj.find(id1)->second;
        std::set<uint32_t>& neis2 = adj.find(id2)->second;
        if(neis1.find(id2) == neis1.end() && neis2.find(id1) == neis2.end())
        {
            num_undirected_edges++;
        }

        auto iter1 = adj.find(id1);
        auto iter2 = adj.find(id2);
        assert(iter1 != adj.end() && iter2 != adj.end());
        iter1->second.insert(id2);
        iter2->second.insert(id1);

    }
    std::cerr << "conversion done; " << nodes.size() << " nodes and " << num_undirected_edges << " edges; printing\n";

    out << nodes.size() << " " << num_undirected_edges << std::endl;
    for(auto it = nodes.begin(); it != nodes.end(); it++)
    {
        std::set<uint32_t>& neis = adj.find(*it)->second;
        for(auto nit = neis.begin();  nit != neis.end(); nit++)
        {
            out << *nit << " "; 
        }
        out << std::endl;
    }
}
