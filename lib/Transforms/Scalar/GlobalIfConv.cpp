#include "GlobalIfConv.h"

#include "IfConv.h"

#include "llvm/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <boost/config.hpp>
#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>

#include <boost/graph/adjacency_list.hpp>

#include <sys/times.h>



// undirected graph multiway partitioning problem
namespace CFGPartition {
  struct EdgeProp;
  struct NodeProp;

  typedef boost::adjacency_list<boost::listS,
                    boost::vecS,
                  boost::bidirectionalS,
                  NodeProp,
                  EdgeProp> graph_t;
  struct EdgeProp {
    int cost;
    EdgeProp() : cost(0) {}
    EdgeProp(int c) : cost(c) {}
  };
  struct NodeProp {
    std::set<llvm::BasicBlock *> BBs;
    std::string name;
    int weight;
    IfConv::BlockInfo info;
    NodeProp() : weight(0) {}
    NodeProp(llvm::BasicBlock *bb, int w = 0)
      : name(bb->getNameStr()), weight(w) {
      BBs.insert(bb);
    }
  };

  typedef boost::graph_traits<graph_t>::vertex_descriptor node_t;
  typedef boost::graph_traits<graph_t>::edge_descriptor edge_t;

  // cplex decision variables are annotated with this structure
  struct cplex_var_annotation {
    cplex_var_annotation(const edge_t &e) : edge(e) {}
    edge_t edge;
  };

  // Problem formulation and solver
  class Problem {
    static const int TIME_LIMIT = 0;

  private:
    struct Solution {
      Solution(const graph_t &g) : graph(g) {}
      graph_t graph;
      int sumCost() const {
        int c = 0;
        BOOST_FOREACH(const edge_t &e, edges(graph))
          c += graph[e].cost;
        return c;
      }
    };

    // branch & bound state
    std::pair<Solution, int> Incumbent; // best solution and cost
    int LB; // lower bound

    void reduce(const Solution &s);
    void check(const Solution &s);


  public:
    Problem();
    void computeSolution(std::set<llvm::BasicBlock*> &result);

    // original CFG
    graph_t graph;
  private:
  };
}

using namespace llvm;
using namespace boost;
using namespace CFGPartition;

GlobalIfConv::GlobalIfConv(Interval *Int, const IfConv::Oracle &orcl)
  : cfgp(new CFGPartition::Problem) {
  std::map<const BasicBlock*, node_t> BlockMap;

  // last block is the header of the next interval???
  Int->Nodes.pop_back();

  //Int->print(dbgs());

  // add blocks as vertices
  BOOST_FOREACH(BasicBlock *BB, Int->Nodes) {
    node_t n = add_vertex(NodeProp(BB), cfgp->graph);
    orcl.analyze(BB, cfgp->graph[n].info);
    BlockMap[BB] = n;
  }

  // add edges between the blocks
  BOOST_FOREACH(BasicBlock *BB, Int->Nodes) {
    if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
      assert(BlockMap.count(BB));
      unsigned i = 0, e = BI->getNumSuccessors();
      assert(e <= 2);
      for(; i < e; ++i) {
        if (!Int->contains(BI->getSuccessor(i)))
          continue;
        assert(BlockMap.count(BI->getSuccessor(i)));
        int cost = orcl.getEdgeCost(BB, BI->getSuccessor(i));
        add_edge(BlockMap[BB], BlockMap[BI->getSuccessor(i)],
            EdgeProp(cost), cfgp->graph);
      }
    }
  }
  DEBUG(dbgs() << "graph vertices: " << num_vertices(cfgp->graph)
      << " edges: " << num_edges(cfgp->graph) << "\n");
}

GlobalIfConv::~GlobalIfConv() {
  delete cfgp;
}

void GlobalIfConv::solve(std::set<BasicBlock*> &result) {
  cfgp->computeSolution(result);
}

CFGPartition::Problem::Problem()
  : Incumbent(std::make_pair(graph_t(), INT_MAX))
{
}

void CFGPartition::Problem::computeSolution(std::set<BasicBlock*> &result) {
  Solution root(graph);
  Incumbent = std::make_pair(root, INT_MAX);
  reduce(root);

  graph_t &g_solution = Incumbent.first.graph;
  BOOST_FOREACH(const node_t &n, vertices(g_solution)) {
    if (g_solution[n].BBs.size() > 1)
      result.insert(g_solution[n].BBs.begin(), g_solution[n].BBs.end());
  }
}

void CFGPartition::Problem::check(const Solution &s) {
  dbgs() << "Solution X:\ngraph vertices: " << num_vertices(s.graph)
      << " edges: " << num_edges(s.graph) << "\n";
  int cost = s.sumCost();
  dbgs() << "cost: " << cost << "\n";
  if (cost < Incumbent.second) {
    Incumbent = std::make_pair(s, cost);
    dbgs() << "new incumbent\n";
  }
}

void CFGPartition::Problem::reduce(const Solution &s) {
  BOOST_FOREACH(const node_t &v, vertices(s.graph)) {
    if (!s.graph[v].info.Convertible) {
      DEBUG(dbgs() << "cannot collapse " << s.graph[v].name << "\n");
      continue;
    }
    if (in_degree(v, s.graph) == 1) {
      edge_t e = *in_edges(v, s.graph).first;
      node_t u = source(e, s.graph);
      dbgs() << "reduction: " << s.graph[v].name << " -> " << s.graph[u].name
        << "\n";
      // collapse nodes in a new solution
      Solution rs(s.graph);
      rs.graph[u].BBs.insert(rs.graph[v].BBs.begin(), rs.graph[v].BBs.end());
      std::ostringstream ss;
      ss << rs.graph[u].name << "+" << rs.graph[v].name;
      rs.graph[u].name = ss.str();
      // redirect out-edges
      while (out_degree(v, rs.graph)) {
        edge_t f = *out_edges(v, rs.graph).first;
        node_t w = target(f, rs.graph);
        if (!edge(u, w, rs.graph).second)
          add_edge(u, w, rs.graph[f], rs.graph);
        remove_edge(f, rs.graph);
      }
      remove_edge(u, v, rs.graph);
      remove_vertex(v, rs.graph);
      check(rs);
      reduce(rs);
    }
  }
}
// computeSolution()


