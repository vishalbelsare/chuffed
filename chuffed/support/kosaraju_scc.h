#ifndef KOSARAJU_H
#define KOSARAJU_H

#include <queue>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

class KosarajuSCC {
protected:
	int nb_nodes;                             // No. of vertices
	std::vector<std::vector<int> > outgoing;  // node-> outgoing edges
	std::vector<std::vector<int> > ingoing;   // node-> outgoing edgse
	std::vector<std::vector<int> > ends;      // edge -> endnodes

	std::vector<int> scc;
	std::vector<std::vector<int> > sccs;
	std::vector<bool> is_mand;
	std::unordered_map<int, int> level2mandscc;
	std::unordered_map<int, int> mandscc2somenode;
	// Fills Stack with vertices (in increasing order of finishing
	// times). The top element of stack has the maximum finishing
	// time
	void fillOrder(int v, bool visited[], std::stack<int>& s);

	// A recursive function to print DFS starting from v
	void DFS(int v, bool visited[], int curr);

	std::vector<int> levels;

public:
	KosarajuSCC(int v, std::vector<std::vector<int> > outgoing,
							std::vector<std::vector<int> > ingoing, std::vector<std::vector<int> > ends);

	virtual ~KosarajuSCC() = default;

	virtual bool ignore_edge(int /*e*/) { return false; }
	virtual bool ignore_node(int /*n*/) { return false; }
	virtual bool mandatory_node(int /*n*/) { return false; }

	// The main function that finds and prints strongly connected
	// components
	void run();
	inline int scc_of(int u) { return scc[u]; }
	inline std::vector<int> get_scc(int i) { return sccs[i]; }
	int nb_sccs() { return static_cast<int>(sccs.size()); }

	// Levels
	void _set_levels(int start, int sink);
	void topological_sort(int u, std::vector<std::vector<int> >& out,
												std::vector<std::vector<int> >& ends, std::queue<int>& sort,
												std::vector<bool>& seen);
	void _set_levels(int u, bool vis[], std::unordered_map<int, bool>& mscc, int parent = -1,
									 const std::string& des = "");
	void set_levels(int start, int sink);
	inline int level_of_scc(int scc) { return levels[scc]; }
	inline bool scc_mand(int scc) { return is_mand[scc]; }
	inline int mand_scc_level(int level) { return level2mandscc[level]; }
	inline int node_from_mandscc(int mandscc) { return mandscc2somenode[mandscc]; }
};

#endif
