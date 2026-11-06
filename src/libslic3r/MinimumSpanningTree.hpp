#ifndef MINIMUMSPANNINGTREE_H
#define MINIMUMSPANNINGTREE_H

#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "Point.hpp"

namespace Slic3r
{






class MinimumSpanningTree
{






    struct Edge {



        const Point start;




        const Point end;
    };
public:
    MinimumSpanningTree() = default;



    MinimumSpanningTree(std::vector<Point> vertices);





    std::vector<Point> adjacent_nodes(Point node) const;





    std::vector<Point> leaves() const;





    std::vector<Point> vertices() const;

private:
    using AdjacencyGraph_t = std::unordered_map<Point, std::vector<Edge>, PointHash>;
    AdjacencyGraph_t adjacency_graph;








    AdjacencyGraph_t prim(std::vector<Point> vertices) const;
};

}

#endif
