/*  ADMesh -- process triangulated solid meshes
 *  Copyright (C) 1995, 1996  Anthony D. Martin <amartin@engr.csulb.edu>
 *  Copyright (C) 2013, 2014  several contributors, see AUTHORS
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  Questions, comments, suggestions, etc to
 *           https://github.com/admesh/admesh/issues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include <boost/predef/other/endian.h>
#include <boost/log/trivial.hpp>

#define BOOST_POOL_NO_MT
#include <boost/pool/object_pool.hpp>

#include "stl.h"

struct HashEdge {

	uint32_t       key[6];

	bool operator==(const HashEdge &rhs) const { return memcmp(key, rhs.key, sizeof(key)) == 0; }
	bool operator!=(const HashEdge &rhs) const { return ! (*this == rhs); }



	int  hash(int M) const {
		uint64_t h = 0x9E3779B97F4A7C15ull;
		for (uint32_t k : key) {
			h ^= k;
			h *= 0xBF58476D1CE4E5B9ull;
			h ^= h >> 31;
		}
		return int(h % uint64_t(M));
	}


	int        facet_number;


	int        which_edge;
	HashEdge  *next;

	void load_exact(stl_file *stl, const stl_vertex *a, const stl_vertex *b)
	{
		{
            stl_vertex diff = (*a - *b).cwiseAbs();
            float max_diff = std::max(diff(0), std::max(diff(1), diff(2)));
            stl->stats.shortest_edge = std::min(max_diff, stl->stats.shortest_edge);
        }



        if (vertex_lower(*a, *b)) {
        } else {

		    std::swap(a, b);
		    this->which_edge += 3;
        }
        memcpy(&this->key[0], a->data(), sizeof(stl_vertex));
        memcpy(&this->key[3], b->data(), sizeof(stl_vertex));

        for (size_t i = 0; i < 6; ++ i) {
            unsigned char *p = (unsigned char*)(this->key + i);
	#if BOOST_ENDIAN_LITTLE_BYTE
            if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0x80)

                p[3] = 0;
	#else
            if (p[0] == 0x80 && p[1] == 0 && p[2] == 0 && p[3] == 0)

                p[0] = 0;
	#endif
        }
	}

	bool load_nearby(const stl_file *stl, const stl_vertex &a, const stl_vertex &b, float tolerance)
	{

		typedef Eigen::Matrix<int32_t,  3, 1, Eigen::DontAlign> Vec3i32;
		Vec3i32 vertex1 = ((a - stl->stats.min) / tolerance).cast<int32_t>();
		Vec3i32 vertex2 = ((b - stl->stats.min) / tolerance).cast<int32_t>();
		static_assert(sizeof(Vec3i32) == 12, "size of Vec3i32 incorrect");

		if (vertex1 == vertex2)

			return false;



		if ((vertex1[0] != vertex2[0]) ?
		    (vertex1[0] < vertex2[0]) :
		    ((vertex1[1] != vertex2[1]) ?
		        (vertex1[1] < vertex2[1]) :
		        (vertex1[2] < vertex2[2]))) {
			memcpy(&this->key[0], vertex1.data(), sizeof(stl_vertex));
			memcpy(&this->key[3], vertex2.data(), sizeof(stl_vertex));
		} else {
			memcpy(&this->key[0], vertex2.data(), sizeof(stl_vertex));
			memcpy(&this->key[3], vertex1.data(), sizeof(stl_vertex));
			this->which_edge += 3;
		}
		return true;
	}

private:
	inline bool vertex_lower(const stl_vertex &a, const stl_vertex &b) {
        return (a(0) != b(0)) ? (a(0) < b(0)) :
	           ((a(1) != b(1)) ? (a(1) < b(1)) : (a(2) < b(2)));
	}
};

struct HashTableEdges {
	HashTableEdges(size_t number_of_faces) {
		this->M = (int)hash_size_from_nr_faces(number_of_faces);
		this->heads.assign(this->M, nullptr);
		this->tail = pool.construct();
		this->tail->next = this->tail;
		for (int i = 0; i < this->M; ++ i)
			this->heads[i] = this->tail;
	}
	~HashTableEdges() {
#ifndef NDEBUG
		for (int i = 0; i < this->M; ++ i)
            for (HashEdge *temp = this->heads[i]; temp != this->tail; temp = temp->next)
                ++ this->freed;
		this->tail = nullptr;
#endif
	}

	void insert_edge_exact(stl_file *stl, const HashEdge &edge)
	{
		this->insert_edge(stl, edge, [stl](const HashEdge& edge1, const HashEdge& edge2) { record_neighbors(stl, edge1, edge2); });
	}

	void insert_edge_nearby(stl_file *stl, const HashEdge &edge)
	{
		this->insert_edge(stl, edge, [stl](const HashEdge& edge1, const HashEdge& edge2) { match_neighbors_nearby(stl, edge1, edge2); });
	}


	std::vector<HashEdge*> 	heads;
	HashEdge* 				tail;
	int           			M;
	boost::object_pool<HashEdge> pool;

#ifndef NDEBUG
	size_t 					malloced   	= 0;
	size_t 					freed 	  	= 0;
	size_t 					collisions 	= 0;
#endif

private:
	static inline size_t hash_size_from_nr_faces(const size_t nr_faces)
	{


		static std::vector<uint32_t> primes{ 98317, 196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843, 50331653, 100663319, 201326611, 402653189, 805306457, 1610612741 };

		auto it = std::upper_bound(primes.begin(), primes.end(), nr_faces * 3 * 2 - 1);
		return (it == primes.end()) ? primes.back() : *it;
	}



	template<typename MatchNeighbors>
	void insert_edge(stl_file *stl, const HashEdge &edge, MatchNeighbors match_neighbors)
	{
		int       chain_number = edge.hash(this->M);
		HashEdge *link         = this->heads[chain_number];
		if (link == this->tail) {

			HashEdge *new_edge = pool.construct(edge);
#ifndef NDEBUG
			++ this->malloced;
#endif
			new_edge->next = this->tail;
			this->heads[chain_number] = new_edge;
		} else if (edges_equal(edge, *link)) {

			match_neighbors(edge, *link);

			this->heads[chain_number] = link->next;

#ifndef NDEBUG
			++ this->freed;
#endif
		} else {

			for (;;) {
				if (link->next == this->tail) {

					HashEdge *new_edge = pool.construct();
#ifndef NDEBUG
					++ this->malloced;
#endif
					*new_edge = edge;
					new_edge->next = this->tail;
					link->next = new_edge;
#ifndef NDEBUG
					++ this->collisions;
#endif
					break;
				}
				if (edges_equal(edge, *link->next)) {

					match_neighbors(edge, *link->next);

					HashEdge *temp = link->next;
					link->next = link->next->next;

#ifndef NDEBUG
					++ this->freed;
#endif
					break;
				}

				link = link->next;
#ifndef NDEBUG
				++ this->collisions;
#endif
			}
		}
	}


	static inline bool edges_equal(const HashEdge &edge_a, const HashEdge &edge_b)
	{
	    return edge_a.facet_number != edge_b.facet_number && edge_a == edge_b;
	}


	static void record_neighbors(stl_file *stl, const HashEdge &edge_a, const HashEdge &edge_b)
	{

		stl->neighbors_start[edge_a.facet_number].neighbor[edge_a.which_edge % 3] = edge_b.facet_number;
		stl->neighbors_start[edge_a.facet_number].which_vertex_not[edge_a.which_edge % 3] = (edge_b.which_edge + 2) % 3;


		stl->neighbors_start[edge_b.facet_number].neighbor[edge_b.which_edge % 3] = edge_a.facet_number;
		stl->neighbors_start[edge_b.facet_number].which_vertex_not[edge_b.which_edge % 3] = (edge_a.which_edge + 2) % 3;

		if ((edge_a.which_edge < 3 && edge_b.which_edge < 3) || (edge_a.which_edge > 2 && edge_b.which_edge > 2)) {

			stl->neighbors_start[edge_a.facet_number].which_vertex_not[edge_a.which_edge % 3] += 3;
			stl->neighbors_start[edge_b.facet_number].which_vertex_not[edge_b.which_edge % 3] += 3;
		}



		stl->stats.connected_edges += 2;

		switch (stl->neighbors_start[edge_a.facet_number].num_neighbors()) {
		case 1:	++ stl->stats.connected_facets_1_edge; break;
		case 2: ++ stl->stats.connected_facets_2_edge; break;
		case 3: ++ stl->stats.connected_facets_3_edge; break;
		default: assert(false);
		}
		switch (stl->neighbors_start[edge_b.facet_number].num_neighbors()) {
		case 1:	++ stl->stats.connected_facets_1_edge; break;
		case 2: ++ stl->stats.connected_facets_2_edge; break;
		case 3: ++ stl->stats.connected_facets_3_edge; break;
		default: assert(false);
		}
	}

	static void match_neighbors_nearby(stl_file *stl, const HashEdge &edge_a, const HashEdge &edge_b)
	{
		record_neighbors(stl, edge_a, edge_b);


		int facet1 = -1;
		int facet2 = -1;
		int vertex1, vertex2;
		stl_vertex new_vertex1, new_vertex2;
		{
			int v1a;
			int v1b;
			int v2a;
			int v2b;

			if (edge_a.which_edge < 3) {
				v1a = edge_a.which_edge;
				v2a = (edge_a.which_edge + 1) % 3;
			} else {
				v2a = edge_a.which_edge % 3;
				v1a = (edge_a.which_edge + 1) % 3;
			}
			if (edge_b.which_edge < 3) {
				v1b = edge_b.which_edge;
				v2b = (edge_b.which_edge + 1) % 3;
			} else {
				v2b = edge_b.which_edge % 3;
				v1b = (edge_b.which_edge + 1) % 3;
			}


			if (stl->facet_start[edge_a.facet_number].vertex[v1a] != stl->facet_start[edge_b.facet_number].vertex[v1b]) {

				if (   (stl->neighbors_start[edge_a.facet_number].neighbor[v1a] == -1)
			        && (stl->neighbors_start[edge_a.facet_number].neighbor[(v1a + 2) % 3] == -1)) {

                    facet1 = edge_a.facet_number;
                    vertex1 = v1a;
                    new_vertex1 = stl->facet_start[edge_b.facet_number].vertex[v1b];
				} else {
                    facet1 = edge_b.facet_number;
                    vertex1 = v1b;
                    new_vertex1 = stl->facet_start[edge_a.facet_number].vertex[v1a];
				}
			}


			if (stl->facet_start[edge_a.facet_number].vertex[v2a] != stl->facet_start[edge_b.facet_number].vertex[v2b]) {

				if (  (stl->neighbors_start[edge_a.facet_number].neighbor[v2a] == -1)
			       && (stl->neighbors_start[edge_a.facet_number].neighbor[(v2a + 2) % 3] == -1)) {

                    facet2 = edge_a.facet_number;
                    vertex2 = v2a;
                    new_vertex2 = stl->facet_start[edge_b.facet_number].vertex[v2b];
				} else {
                    facet2 = edge_b.facet_number;
                    vertex2 = v2b;
                    new_vertex2 = stl->facet_start[edge_a.facet_number].vertex[v2a];
				}
			}
		}

		auto change_vertices = [stl](int facet_num, int vnot, stl_vertex new_vertex)
		{
			int first_facet = facet_num;
			bool direction = false;

			for (;;) {
				int pivot_vertex;
				int next_edge;
				if (vnot > 2) {
					if (direction) {
						pivot_vertex = (vnot + 1) % 3;
						next_edge = vnot % 3;
					}
					else {
						pivot_vertex = (vnot + 2) % 3;
						next_edge = pivot_vertex;
					}
					direction = !direction;
				}
				else {
					if (direction) {
						pivot_vertex = (vnot + 2) % 3;
						next_edge = pivot_vertex;
					}
					else {
						pivot_vertex = (vnot + 1) % 3;
						next_edge = vnot;
					}
				}
	#if 0
				if (stl->facet_start[facet_num].vertex[pivot_vertex](0) == new_vertex(0) &&
					stl->facet_start[facet_num].vertex[pivot_vertex](1) == new_vertex(1) &&
					stl->facet_start[facet_num].vertex[pivot_vertex](2) == new_vertex(2))
					printf("Changing vertex %f,%f,%f: Same !!!\r\n", new_vertex(0), new_vertex(1), new_vertex(2));
				else {
					if (stl->facet_start[facet_num].vertex[pivot_vertex](0) != new_vertex(0))
						printf("Changing coordinate x, vertex %e (0x%08x) to %e(0x%08x)\r\n",
							stl->facet_start[facet_num].vertex[pivot_vertex](0),
							*reinterpret_cast<const int*>(&stl->facet_start[facet_num].vertex[pivot_vertex](0)),
							new_vertex(0),
							*reinterpret_cast<const int*>(&new_vertex(0)));
					if (stl->facet_start[facet_num].vertex[pivot_vertex](1) != new_vertex(1))
						printf("Changing coordinate x, vertex %e (0x%08x) to %e(0x%08x)\r\n",
							stl->facet_start[facet_num].vertex[pivot_vertex](1),
							*reinterpret_cast<const int*>(&stl->facet_start[facet_num].vertex[pivot_vertex](1)),
							new_vertex(1),
							*reinterpret_cast<const int*>(&new_vertex(1)));
					if (stl->facet_start[facet_num].vertex[pivot_vertex](2) != new_vertex(2))
						printf("Changing coordinate x, vertex %e (0x%08x) to %e(0x%08x)\r\n",
							stl->facet_start[facet_num].vertex[pivot_vertex](2),
							*reinterpret_cast<const int*>(&stl->facet_start[facet_num].vertex[pivot_vertex](2)),
							new_vertex(2),
							*reinterpret_cast<const int*>(&new_vertex(2)));
				}
	#endif
				stl->facet_start[facet_num].vertex[pivot_vertex] = new_vertex;
				vnot = stl->neighbors_start[facet_num].which_vertex_not[next_edge];
				facet_num = stl->neighbors_start[facet_num].neighbor[next_edge];
				if (facet_num == -1)
					break;

				if (facet_num == first_facet) {

					BOOST_LOG_TRIVIAL(info) << "Back to the first facet changing vertices: probably a mobius part. Try using a smaller tolerance or don't do a nearby check.";
					return;
				}
			}
		};

		if (facet1 != -1) {
			int vnot1 = (facet1 == edge_a.facet_number) ?
                (edge_a.which_edge + 2) % 3 :
				(edge_b.which_edge + 2) % 3;
			if (((vnot1 + 2) % 3) == vertex1)
                vnot1 += 3;
			change_vertices(facet1, vnot1, new_vertex1);
		}
		if (facet2 != -1) {
			int vnot2 = (facet2 == edge_a.facet_number) ?
                (edge_a.which_edge + 2) % 3 :
				(edge_b.which_edge + 2) % 3;
			if (((vnot2 + 2) % 3) == vertex2)
                vnot2 += 3;
			change_vertices(facet2, vnot2, new_vertex2);
		}
		stl->stats.edges_fixed += 2;
	}
};




void stl_check_facets_exact(stl_file *stl)
{
	assert(stl->facet_start.size() == stl->neighbors_start.size());

    stl->stats.connected_edges         = 0;
    stl->stats.connected_facets_1_edge = 0;
    stl->stats.connected_facets_2_edge = 0;
    stl->stats.connected_facets_3_edge = 0;




    for (uint32_t i = 0; i < stl->stats.number_of_facets;) {
		stl_facet &facet = stl->facet_start[i];
        if (facet.vertex[0] == facet.vertex[1] || facet.vertex[1] == facet.vertex[2] || facet.vertex[0] == facet.vertex[2]) {

            facet = stl->facet_start[-- stl->stats.number_of_facets];
			stl->facet_start.pop_back();
			stl->neighbors_start.pop_back();
            stl->stats.facets_removed += 1;
            stl->stats.degenerate_facets += 1;
        } else
            ++ i;
    }


    HashTableEdges hash_table(stl->stats.number_of_facets);
	for (auto &neighbor : stl->neighbors_start)
		neighbor.reset();


	for (uint32_t i = 0; i < stl->stats.number_of_facets; ++ i) {
		const stl_facet &facet = stl->facet_start[i];
		for (int j = 0; j < 3; ++ j) {
			HashEdge edge;
			edge.facet_number = i;
			edge.which_edge = j;
			edge.load_exact(stl, &facet.vertex[j], &facet.vertex[(j + 1) % 3]);
			hash_table.insert_edge_exact(stl, edge);
		}
	}

#if 0
	printf("Number of faces: %d, number of manifold edges: %d, number of connected edges: %d, number of unconnected edges: %d\r\n",
        stl->stats.number_of_facets, stl->stats.number_of_facets * 3,
        stl->stats.connected_edges, stl->stats.number_of_facets * 3 - stl->stats.connected_edges);
#endif
}

void stl_check_facets_nearby(stl_file *stl, float tolerance)
{
	assert(stl->stats.connected_facets_3_edge <= stl->stats.connected_facets_2_edge);
	assert(stl->stats.connected_facets_2_edge <= stl->stats.connected_facets_1_edge);
	assert(stl->stats.connected_facets_1_edge <= stl->stats.number_of_facets);

    if (stl->stats.connected_facets_3_edge == stl->stats.number_of_facets)

        return;

    HashTableEdges hash_table(stl->stats.number_of_facets);
    for (uint32_t i = 0; i < stl->stats.number_of_facets; ++ i) {

        stl_facet facet = stl->facet_start[i];
        for (int j = 0; j < 3; j++) {
            if (stl->neighbors_start[i].neighbor[j] == -1) {
                HashEdge edge;
                edge.facet_number = i;
                edge.which_edge = j;
                if (edge.load_nearby(stl, facet.vertex[j], facet.vertex[(j + 1) % 3], tolerance))

                    hash_table.insert_edge_nearby(stl, edge);
            }
        }
    }
}

void stl_remove_unconnected_facets(stl_file *stl)
{



	auto remove_facet = [stl](int facet_number)
	{
		++ stl->stats.facets_removed;

		stl_neighbors &neighbors = stl->neighbors_start[facet_number];

		switch (neighbors.num_neighbors()) {
		case 3: -- stl->stats.connected_facets_3_edge; // fall through
		case 2: -- stl->stats.connected_facets_2_edge; // fall through
		case 1: -- stl->stats.connected_facets_1_edge; // fall through
		case 0: break;
		default: assert(false);
		}

        if (facet_number < int(-- stl->stats.number_of_facets)) {


            stl->facet_start[facet_number] = stl->facet_start[stl->stats.number_of_facets];
            neighbors = stl->neighbors_start[stl->stats.number_of_facets];

            for (int i = 0; i < 3; ++ i)
                if (neighbors.neighbor[i] != -1) {
                    int &other_face_idx = stl->neighbors_start[neighbors.neighbor[i]].neighbor[(neighbors.which_vertex_not[i] + 1) % 3];
                    if (other_face_idx != stl->stats.number_of_facets) {
                        BOOST_LOG_TRIVIAL(info) << "in remove_facet: neighbor = " << other_face_idx << " numfacets = " << stl->stats.number_of_facets << " this is wrong";
                        return;
                    }
                    other_face_idx = facet_number;
                }
		}

        stl->facet_start.pop_back();
        stl->neighbors_start.pop_back();
	};

	auto remove_degenerate = [stl, remove_facet](int facet)
	{

		auto update_connects_remove_1 = [stl](int facet_num) {
			switch (stl->neighbors_start[facet_num].num_neighbors()) {
			case 0: assert(false); break;
			case 1: -- stl->stats.connected_facets_1_edge; break;
			case 2: -- stl->stats.connected_facets_2_edge; break;
			case 3: -- stl->stats.connected_facets_3_edge; break;
			default: assert(false);
            }
		};

		int edge_to_collapse = 0;
        if (stl->facet_start[facet].vertex[0] == stl->facet_start[facet].vertex[1]) {
			if (stl->facet_start[facet].vertex[1] == stl->facet_start[facet].vertex[2]) {

				const int *nbr = stl->neighbors_start[facet].neighbor;
				edge_to_collapse = (nbr[0] == -1) ? 0 : (nbr[1] == -1) ? 1 : 2;
			} else {
				edge_to_collapse = 0;
			}
        } else if (stl->facet_start[facet].vertex[1] == stl->facet_start[facet].vertex[2]) {
			edge_to_collapse = 1;
        } else if (stl->facet_start[facet].vertex[2] == stl->facet_start[facet].vertex[0]) {
			edge_to_collapse = 2;
        } else {

            return;
        }

		int edge[3] = { (edge_to_collapse + 1) % 3, (edge_to_collapse + 2) % 3, edge_to_collapse };
		int neighbor[] = {
			stl->neighbors_start[facet].neighbor[edge[0]],
			stl->neighbors_start[facet].neighbor[edge[1]],
			stl->neighbors_start[facet].neighbor[edge[2]]
		};
		int vnot[] = {
			stl->neighbors_start[facet].which_vertex_not[edge[0]],
			stl->neighbors_start[facet].which_vertex_not[edge[1]],
			stl->neighbors_start[facet].which_vertex_not[edge[2]]
		};


		if ((neighbor[0] == -1) && (neighbor[1] != -1))
			update_connects_remove_1(neighbor[1]);
		if ((neighbor[1] == -1) && (neighbor[0] != -1))
			update_connects_remove_1(neighbor[0]);

        if (neighbor[0] >= 0) {
			if (neighbor[1] >= 0) {

				if (vnot[0] > 2) {
					if (vnot[1] > 2) {


						vnot[0] -= 3;
						vnot[1] -= 3;
					} else


						vnot[1] += 3;
				} else if (vnot[1] > 2)


					vnot[0] += 3;
			}
			stl->neighbors_start[neighbor[0]].neighbor[(vnot[0] + 1) % 3] = (neighbor[0] == neighbor[1]) ? -1 : neighbor[1];
            stl->neighbors_start[neighbor[0]].which_vertex_not[(vnot[0] + 1) % 3] = vnot[1];
        }
        if (neighbor[1] >= 0) {
			stl->neighbors_start[neighbor[1]].neighbor[(vnot[1] + 1) % 3] = (neighbor[0] == neighbor[1]) ? -1 : neighbor[0];
            stl->neighbors_start[neighbor[1]].which_vertex_not[(vnot[1] + 1) % 3] = vnot[0];
        }
		if (neighbor[2] >= 0) {
			update_connects_remove_1(neighbor[2]);
			stl->neighbors_start[neighbor[2]].neighbor[(vnot[2] + 1) % 3] = -1;
		}

        remove_facet(facet);
	};


	for (uint32_t i = 0; i < stl->stats.number_of_facets;)
		if (stl->facet_start[i].vertex[0] == stl->facet_start[i].vertex[1] ||
			stl->facet_start[i].vertex[0] == stl->facet_start[i].vertex[2] ||
			stl->facet_start[i].vertex[1] == stl->facet_start[i].vertex[2]) {
			remove_degenerate(i);

		} else
			++ i;

	if (stl->stats.connected_facets_1_edge < (int)stl->stats.number_of_facets) {

		for (uint32_t i = 0; i < stl->stats.number_of_facets;)
			if (stl->neighbors_start[i].num_neighbors() == 0) {

				remove_facet(i);
				assert(stl_validate(stl));
			} else
				++ i;
	}
}

void stl_fill_holes(stl_file *stl)
{

	HashTableEdges hash_table(stl->stats.number_of_facets);
	for (uint32_t i = 0; i < stl->stats.number_of_facets; ++ i) {
        stl_facet facet = stl->facet_start[i];
		for (int j = 0; j < 3; ++ j) {
            if(stl->neighbors_start[i].neighbor[j] != -1)
                continue;
			HashEdge edge;
            edge.facet_number = i;
            edge.which_edge = j;
            edge.load_exact(stl, &facet.vertex[j], &facet.vertex[(j + 1) % 3]);
            hash_table.insert_edge_exact(stl, edge);
		}
	}

	for (uint32_t i = 0; i < stl->stats.number_of_facets; ++ i) {
		stl_facet facet = stl->facet_start[i];
		int neighbors_initial[3] = { stl->neighbors_start[i].neighbor[0], stl->neighbors_start[i].neighbor[1], stl->neighbors_start[i].neighbor[2] };
		int first_facet = i;
		for (int j = 0; j < 3; ++ j) {
            if (stl->neighbors_start[i].neighbor[j] != -1)
                continue;

            stl_facet new_facet;
            new_facet.vertex[0] = facet.vertex[j];
            new_facet.vertex[1] = facet.vertex[(j + 1) % 3];
            bool direction = neighbors_initial[(j + 2) % 3] == -1;
            int facet_num = i;
            int vnot = (j + 2) % 3;

            for (;;) {
				int pivot_vertex = 0;
				int next_edge = 0;
                if (vnot > 2) {
                    if (direction) {
                        pivot_vertex = (vnot + 1) % 3;
                        next_edge = vnot % 3;
                    } else {
                        pivot_vertex = (vnot + 2) % 3;
                        next_edge = pivot_vertex;
                    }
                    direction = ! direction;
                } else {
                    if(direction == 0) {
                        pivot_vertex = (vnot + 1) % 3;
                        next_edge = vnot;
                    } else {
                        pivot_vertex = (vnot + 2) % 3;
                        next_edge = pivot_vertex;
                    }
                }

                int next_facet = stl->neighbors_start[facet_num].neighbor[next_edge];
                if (next_facet == -1) {
                    new_facet.vertex[2] = stl->facet_start[facet_num].vertex[vnot % 3];
				    stl_add_facet(stl, &new_facet);
                    for (int k = 0; k < 3; ++ k) {
                        HashEdge edge;
                        edge.facet_number = stl->stats.number_of_facets - 1;
                        edge.which_edge = k;
                        edge.load_exact(stl, &new_facet.vertex[k], &new_facet.vertex[(k + 1) % 3]);
                        hash_table.insert_edge_exact(stl, edge);
                    }
                    break;
                }

                vnot = stl->neighbors_start[facet_num].which_vertex_not[next_edge];
                facet_num = next_facet;

                if (facet_num == first_facet) {

                    BOOST_LOG_TRIVIAL(info) << "Back to the first facet filling holes: probably a mobius part. Try using a smaller tolerance or don't do a nearby check.";
                    return;
                }
            }
		}
	}
}

void stl_add_facet(stl_file *stl, const stl_facet *new_facet)
{
	assert(stl->facet_start.size() == stl->stats.number_of_facets);
	assert(stl->neighbors_start.size() == stl->stats.number_of_facets);
	stl->facet_start.emplace_back(*new_facet);

    stl->facet_start[stl->stats.number_of_facets].normal = stl_normal::Zero();
    stl->neighbors_start.emplace_back();
	++ stl->stats.facets_added;
	++ stl->stats.number_of_facets;
}
