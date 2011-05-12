#include "GlobalIfConv.h"

#include "IfConv.h"

#include "llvm/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <ilcplex/ilocplex.h>

#include <boost/config.hpp>
#include <boost/foreach.hpp>
#include <boost/tuple/tuple.hpp>

#include <boost/graph/adjacency_list.hpp>

#include <sys/times.h>

#define EPS 1e-6
#define ILOISTRUE(a) (a > (1-EPS))

ILOSTLBEGIN


// undirected graph multiway partitioning problem
namespace CFGPartition {
  struct EdgeProp;
  struct NodeProp;

  typedef boost::adjacency_list<boost::listS,
                    boost::vecS,
                  boost::undirectedS,
                  NodeProp,
                  EdgeProp> graph_t;
  struct EdgeProp {
    int cost;
    IloBoolVar var;
    EdgeProp() : cost(0) {}
    EdgeProp(int c) : cost(c) {}
  };
  struct NodeProp {
    llvm::BasicBlock *block;
    std::string name;
    int weight;
    NodeProp() : block(0), weight(0) {}
    NodeProp(llvm::BasicBlock *bb, int w = 0)
      : block(bb), name(bb->getNameStr()), weight(w) {}
  };

  typedef boost::graph_traits<graph_t>::vertex_descriptor node_t;
  typedef boost::graph_traits<graph_t>::edge_descriptor edge_t;

  // cplex decision variables are annotated with this structure
  struct cplex_var_annotation {
    cplex_var_annotation(const edge_t &e) : edge(e) {}
    edge_t edge;
  };

  // Problem formulation and CPLEX solver
  class Problem {
    static const int TIME_LIMIT = 0;

  private:
    IloCplex cplex;
    IloEnv env;
    IloModel model;

    IloBoolVarArray x_vars;                  /* edge selection variables */
    IloBoolVarArray z_vars;                  /* node selection variables */
    IloIntVarArray f_vars;                   /* flow variables */

    IloNumArray vals;                /* to store result values of x */

    double epInt;


  public:
    Problem();
    void computeSolution(std::set<llvm::BasicBlock*> &result);

    graph_t graph;
  private:
    void setCplexParameters();
    void createModel();
    void cleanup();
  };
}

using namespace llvm;
using namespace boost;
using namespace CFGPartition;

GlobalIfConv::GlobalIfConv(llvm::Function &F, const IfConv::Oracle &orcl)
  : cfgp(new CFGPartition::Problem) {
  std::map<const BasicBlock*, node_t> BlockMap;

  // add blocks as vertices
  for (Function::iterator BBIt = F.begin(); BBIt != F.end(); ++BBIt) {
    BasicBlock *BB = &*BBIt;
    node_t n = add_vertex(NodeProp(BB), cfgp->graph);
    BlockMap[BB] = n;
  }

  // add edges between the blocks
  for (Function::iterator BBIt = F.begin(); BBIt != F.end(); ++BBIt) {
    BasicBlock *BB = &*BBIt;
    if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
      assert(BlockMap.count(BB));
      unsigned i = 0, e = BI->getNumSuccessors();
      assert(e <= 2);
      for(; i < e; ++i) {
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
	: model(env)
	, x_vars(env)
	, z_vars(env)
	, vals(env)
{

}

void CFGPartition::Problem::computeSolution(std::set<BasicBlock*> &result)
{
	unsigned objective_value = 0;
	enum {
		TRIVIAL,
		TIMEOUT,
		OPTIMAL,
		FAILURE
	} status = FAILURE;


	try{
		cplex = IloCplex(env);

		setCplexParameters();

		//if(!Global::inst()->verbose)
		//  cplex.setOut(env.getNullStream());

		if(TIME_LIMIT > 0) {
			std::cerr << "setting timelimit to " << TIME_LIMIT << " seconds..." << endl;
			cplex.setParam(IloCplex::TiLim, TIME_LIMIT);
		}
	}
	catch (IloException& e) {
		std::cerr << "Concert exception caught: " << e << endl;
		assert(0);
	}
	catch (...) {
		std::cerr << "Unknown exception caught" << endl;
		assert(0);
	}

	createModel();

	// extract from Concert and dump to file
	cplex.extract(model);
	cplex.exportModel("part.lp");

	//run the solver
	std::cerr << "running solver..." << endl;

	struct tms tms_old, tms_new;
	times (&tms_old);

	bool have_solution = true;
	if(!cplex.solve()) {
		std::cerr << "failed to optimize LP" << endl;
		have_solution = false;
		status = FAILURE;
	}
	times (&tms_new);
	clock_t elap_user, herz = sysconf(_SC_CLK_TCK);
	elap_user = (tms_new.tms_utime - tms_old.tms_utime);

	std::cout << "solver time (user): " << (elap_user / (double)herz) << " [sec]" << endl;

	bool is_optimal = cplex.getCplexStatus() == CPX_STAT_OPTIMAL
		|| cplex.getCplexStatus() == CPXMIP_OPTIMAL_TOL;

	if(cplex.getCplexStatus() == IloCplex::AbortTimeLim) {
		std::cout << "time limit of " << TIME_LIMIT << " seconds exceeded!" << endl;
		status = TIMEOUT;
	}

	std::cout << "cplex finished with status: " << cplex.getStatus()
		<<" [" << cplex.getCplexStatus() << "]" << endl;
	std::cout << "best bound: " << cplex.getBestObjValue() << endl;

	if(have_solution) {
		objective_value = (unsigned)cplex.getObjValue();
		std::cerr << "objective function value: " << objective_value << endl;
		if(is_optimal) {
			std::cerr << "model solved to optimality!" << endl;
			status = OPTIMAL;
		}
		else
			std::cerr << "failed to find optimal solution!" << endl;

		//back-translate solution
		cplex.getValues(vals, x_vars);
		for(int i = 0, n = x_vars.getSize(); i < n; ++i) {
			if(ILOISTRUE(vals[i])) {
				cerr << x_vars[i].getName() << " in solution" << endl;

        cplex_var_annotation *info =
          (cplex_var_annotation *) x_vars[i].getObject();
        // add nodes connected by this edges to result set
        result.insert(graph[source(info->edge, graph)].block);
        result.insert(graph[target(info->edge, graph)].block);
      }
		}
	}

  cleanup();
	cplex.clearModel();
	model.end();
} // computeSolution()

void CFGPartition::Problem::createModel()
{
	using namespace boost;

	IloExpr objective(env);

	// one auxiliary variable per node
#if 0
	for (unsigned n = 0; n < problem.nNodes; ++n) {
		std::ostringstream name;
		name << "z::" << n;
		IloBoolVar z(env, name.str().c_str());
		z_vars.add(z);
	}
#endif

	BOOST_FOREACH(const edge_t &e, edges(graph)) {
		std::ostringstream name;

		// one decision variable per edge
		name << "x::" << graph[source(e, graph)].name
			<< "::" << graph[target(e, graph)].name;
		IloBoolVar x(env, name.str().c_str());
    cplex_var_annotation *info = new cplex_var_annotation(e);
    x.setObject(info);
		x_vars.add(x);
		name.str("");

		// store variable with edge
		graph[e].var = x;

		// link edge activity with adjacent nodes
		//model.add(z_vars[u] >= x);
		//model.add(z_vars[v] >= x);

		// add (x * cost) to the objective function
		objective += x * (IloNum) graph[e].cost;
	}

	// limit number of edges to k-1 (disregard artifical root)
	//model.add(IloSum(x_vars) == (IloNum) (k - 1)).setName("xcount");

	// limit number of nodes to k
	//model.add(IloSum(z_vars) == (IloNum) k).setName("zcount");


	// add objective
	model.add(IloMaximize(env, objective));

	// triangle inequalities
	unsigned count = 0;
	typedef std::set<node_t> vset_t;
	typedef graph_traits<graph_t>::adjacency_iterator adj_iter_t;
	BOOST_FOREACH(const edge_t &e, edges(graph)) {
		node_t u = source(e, graph);
		node_t v = target(e, graph);
		unsigned max_degree = std::max(degree(u, graph), degree(v, graph));
		std::vector<node_t> inter(max_degree);
		std::pair<adj_iter_t, adj_iter_t>
			u_adj = adjacent_vertices(u, graph),
			v_adj = adjacent_vertices(v, graph);
		std::vector<node_t>::iterator sect_end =
			set_intersection(u_adj.first, u_adj.second, v_adj.first, v_adj.second, inter.begin());
		inter.erase(sect_end, inter.end());

		BOOST_FOREACH(const node_t &w, inter) {
			if (w == u || w == v)
				continue;
			std::cout << "triangle: " << graph[u].name << "-" << graph[v].name
				<< "-" << graph[w].name << std::endl;
			// constraint: (u,w) + (v,w) - (u,v) <= 1
			IloExpr triangle(env);
			triangle =
			      graph[edge(u, w, graph).first].var +
			      graph[edge(v, w, graph).first].var -
			      graph[edge(u, v, graph).first].var;
			std::ostringstream name;
			name << "tri" << count;
			model.add(triangle <= (IloInt) 1).setName(name.str().c_str());
			count++;
		}
	}



	// create cut-callback
	//IloCplex::Callback usercuts = CFGPartition_CutCallback::create(env, x_vars, z_vars);

	//cplex.use(usercuts);
}

void CFGPartition::Problem::cleanup() {
  for(int i = 0, n = x_vars.getSize(); i < n; ++i) {
    delete (cplex_var_annotation *) x_vars[i].getObject();
    x_vars[i].setObject(NULL);
  }
}

void CFGPartition::Problem::setCplexParameters()
{
#if 0
   cplex.setParam(IloCplex::Covers, -1);
   cplex.setParam(IloCplex::Cliques, -1);
   cplex.setParam(IloCplex::DisjCuts, -1);
   cplex.setParam(IloCplex::FlowCovers, -1);
   cplex.setParam(IloCplex::FlowPaths, -1);

   cplex.setParam(IloCplex::FracCuts, -1);
   cplex.setParam(IloCplex::GUBCovers, -1);
   cplex.setParam(IloCplex::ImplBd, -1);
   cplex.setParam(IloCplex::MIRCuts, -1);

   cplex.setParam(IloCplex::AggCutLim, 10000);
   cplex.setParam(IloCplex::CutPass, -1); // 0 = auto
#endif

   cplex.setParam(IloCplex::EpInt, EPS);

} // setCplexParameters

