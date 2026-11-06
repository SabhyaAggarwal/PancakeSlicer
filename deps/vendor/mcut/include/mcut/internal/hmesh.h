/**
 * Copyright (c) 2021-2022 Floyd M. Chitalu.
 * All rights reserved.
 *
 * NOTE: This file is licensed under GPL-3.0-or-later (default).
 * A commercial license can be purchased from Floyd M. Chitalu.
 *
 * License details:
 *
 * (A)  GNU General Public License ("GPL"); a copy of which you should have
 *      recieved with this file.
 * 	    - see also: <http://www.gnu.org/licenses/>
 * (B)  Commercial license.
 *      - email: floyd.m.chitalu@gmail.com
 *
 * The commercial license options is for users that wish to use MCUT in
 * their products for comercial purposes but do not wish to release their
 * software products under the GPL license.
 *
 * Author(s)     : Floyd M. Chitalu
 */

#ifndef MCUT_HALFEDGE_MESH_H_
#define MCUT_HALFEDGE_MESH_H_

#include "mcut/internal/math.h"
#include "mcut/internal/utils.h"

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <vector>
#include <cstdint>

template <typename T>
class descriptor_t_ {
public:
    typedef unsigned int index_type;
    descriptor_t_() { }
    virtual ~descriptor_t_() { }
    explicit descriptor_t_(index_type i = (std::numeric_limits<index_type>::max)())
        : m_value(i)
    {
    }

    operator index_type() const { return m_value; }

    void reset() { m_value = (std::numeric_limits<index_type>::max)(); }

    bool is_valid() const
    {
        index_type inf = (std::numeric_limits<index_type>::max)();
        return m_value != inf;
    }

    descriptor_t_& operator=(const index_type& _rhs)
    {
        m_value = _rhs;
        return *this;
    }

    descriptor_t_& operator=(const T& _rhs) const
    {
        m_value = _rhs.m_value;
        return *this;
    }

    bool operator==(const T& _rhs) const
    {
        return m_value == _rhs.m_value;
    }

    bool operator!=(const T& _rhs) const
    {
        return m_value != _rhs.m_value;
    }

    bool operator<(const T& _rhs) const
    {
        return m_value < _rhs.m_value;
    }

    descriptor_t_& operator++()
    {
        ++m_value;
        return *this;
    }

    descriptor_t_& operator--()
    {
        --m_value;
        return *this;
    }

    descriptor_t_ operator++(int)
    {
        descriptor_t_ tmp(*this);
        ++m_value;
        return tmp;
    }

    descriptor_t_ operator--(int)
    {
        descriptor_t_ tmp(*this);
        --m_value;
        return tmp;
    }

    descriptor_t_& operator+=(std::ptrdiff_t n)
    {
        m_value = (unsigned int)(m_value + n);
        return *this;
    }

protected:
    unsigned int m_value;
};

class halfedge_descriptor_t : public descriptor_t_<halfedge_descriptor_t> {
public:
    halfedge_descriptor_t()
        : descriptor_t_<halfedge_descriptor_t>(std::numeric_limits<index_type>::max())
    {
    }

    explicit halfedge_descriptor_t(descriptor_t_<halfedge_descriptor_t>::index_type idx)
        : descriptor_t_<halfedge_descriptor_t>(idx)
    {
    }

    virtual ~halfedge_descriptor_t()
    {
    }
};

class edge_descriptor_t : public descriptor_t_<edge_descriptor_t> {
public:
    edge_descriptor_t()
        : descriptor_t_<edge_descriptor_t>((std::numeric_limits<index_type>::max)())
    {
    }

    explicit edge_descriptor_t(descriptor_t_<edge_descriptor_t>::index_type idx)
        : descriptor_t_<edge_descriptor_t>(idx)
    {
    }

    virtual ~edge_descriptor_t()
    {
    }
};

class face_descriptor_t : public descriptor_t_<face_descriptor_t> {
public:
    face_descriptor_t()
        : descriptor_t_<face_descriptor_t>((std::numeric_limits<index_type>::max)())
    {
    }

    explicit face_descriptor_t(descriptor_t_<face_descriptor_t>::index_type idx)
        : descriptor_t_<face_descriptor_t>(idx)
    {
    }

    virtual ~face_descriptor_t()
    {
    }
};

class vertex_descriptor_t : public descriptor_t_<vertex_descriptor_t> {
public:
    vertex_descriptor_t()
        : descriptor_t_<vertex_descriptor_t>((std::numeric_limits<index_type>::max)())
    {
    }

    explicit vertex_descriptor_t(descriptor_t_<vertex_descriptor_t>::index_type idx)
        : descriptor_t_<vertex_descriptor_t>(idx)
    {
    }

    virtual ~vertex_descriptor_t()
    {
    }
};

template <typename T>
struct id_ {
    typedef T type;
};

template <typename V>
class array_iterator_t;

struct halfedge_data_t : id_<halfedge_descriptor_t> {
    vertex_descriptor_t t;
    face_descriptor_t f;
    halfedge_descriptor_t o;
    halfedge_descriptor_t n;
    halfedge_descriptor_t p;
    edge_descriptor_t e;


    halfedge_data_t()

    {
    }
};

struct edge_data_t : id_<edge_descriptor_t> {
    halfedge_descriptor_t h;
};

struct face_data_t : id_<face_descriptor_t> {
    std::vector<halfedge_descriptor_t> m_halfedges;
};

struct vertex_data_t : id_<vertex_descriptor_t> {
    vec3 p;

    std::vector<halfedge_descriptor_t> m_halfedges;
};

typedef std::vector<vertex_data_t> vertex_array_t;
typedef std::vector<edge_data_t> edge_array_t;
typedef std::vector<halfedge_data_t> halfedge_array_t;
typedef std::vector<face_data_t> face_array_t;

typedef array_iterator_t<face_array_t> face_array_iterator_t;
typedef array_iterator_t<vertex_array_t> vertex_array_iterator_t;
typedef array_iterator_t<edge_array_t> edge_array_iterator_t;
typedef array_iterator_t<halfedge_array_t> halfedge_array_iterator_t;


















class hmesh_t {
public:
    hmesh_t();
    ~hmesh_t();




    static vertex_descriptor_t null_vertex();
    static halfedge_descriptor_t null_halfedge();
    static edge_descriptor_t null_edge();
    static face_descriptor_t null_face();





    int number_of_vertices() const;
    int number_of_edges() const;
    int number_of_halfedges() const;
    int number_of_faces() const;

    vertex_descriptor_t source(const halfedge_descriptor_t& h) const;
    vertex_descriptor_t target(const halfedge_descriptor_t& h) const;
    halfedge_descriptor_t opposite(const halfedge_descriptor_t& h) const;
    halfedge_descriptor_t prev(const halfedge_descriptor_t& h) const;
    halfedge_descriptor_t next(const halfedge_descriptor_t& h) const;

    void set_next(const halfedge_descriptor_t& h, const halfedge_descriptor_t& nxt);
    void set_previous(const halfedge_descriptor_t& h, const halfedge_descriptor_t& prev);

    edge_descriptor_t edge(const halfedge_descriptor_t& h) const;
    face_descriptor_t face(const halfedge_descriptor_t& h) const;

    vertex_descriptor_t vertex(const edge_descriptor_t e, const int v) const;

    bool is_border(const halfedge_descriptor_t h);
    bool is_border(const edge_descriptor_t e);

    halfedge_descriptor_t halfedge(const edge_descriptor_t e, const int i) const;

    halfedge_descriptor_t halfedge(const vertex_descriptor_t s, const vertex_descriptor_t t, bool strict_check = false) const;

    edge_descriptor_t edge(const vertex_descriptor_t s, const vertex_descriptor_t t, bool strict_check = false) const;

    vertex_descriptor_t add_vertex(const vec3& point);

    vertex_descriptor_t add_vertex(const double& x, const double& y, const double& z);


    halfedge_descriptor_t add_edge(const vertex_descriptor_t v0, const vertex_descriptor_t v1);
    face_descriptor_t add_face(const std::vector<vertex_descriptor_t>& vi);



    bool is_insertable(const std::vector<vertex_descriptor_t> &vi) const;


    void remove_face(const face_descriptor_t f);

    void remove_halfedge(halfedge_descriptor_t h);

    void remove_edge(const edge_descriptor_t e, bool remove_halfedges = true);
    void remove_vertex(const vertex_descriptor_t v);
    void remove_elements();

    void reset();

    int number_of_internal_faces() const;
    int number_of_internal_edges() const;
    int number_of_internal_halfedges() const;
    int number_of_internal_vertices() const;

    int number_of_vertices_removed() const;
    int number_of_edges_removed() const;
    int number_of_halfedges_removed() const;
    int number_of_faces_removed() const;

    bool is_removed(face_descriptor_t f) const;
    bool is_removed(edge_descriptor_t e) const;
    bool is_removed(halfedge_descriptor_t h) const;
    bool is_removed(vertex_descriptor_t v) const;

    void reserve_for_additional_vertices(std::uint32_t n);
    void reserve_for_additional_edges(std::uint32_t n);
    void reserve_for_additional_halfedges(std::uint32_t n);
    void reserve_for_additional_faces(std::uint32_t n);
    void reserve_for_additional_elements(std::uint32_t additional_vertices);



    template <typename I = int>
    I get_removed_elements(id_<I>)
    {
        return I();
    }

    const std::vector<vertex_descriptor_t>& get_removed_elements(id_<array_iterator_t<vertex_array_t>>) const;
    const std::vector<edge_descriptor_t>& get_removed_elements(id_<array_iterator_t<edge_array_t>>) const;
    const std::vector<halfedge_descriptor_t>& get_removed_elements(id_<array_iterator_t<halfedge_array_t>>) const;
    const std::vector<face_descriptor_t>& get_removed_elements(id_<array_iterator_t<face_array_t>>) const;


    template <typename I = int>
    I elements_begin_(id_<I>)
    {
        return I();
    }

    const vertex_array_iterator_t elements_begin_(id_<vertex_array_iterator_t>, bool account_for_removed_elems = true) const;
    const edge_array_iterator_t elements_begin_(id_<edge_array_iterator_t>, bool account_for_removed_elems = true) const;
    const halfedge_array_iterator_t elements_begin_(id_<halfedge_array_iterator_t>, bool account_for_removed_elems = true) const;
    const face_array_iterator_t elements_begin_(id_<face_array_iterator_t>, bool account_for_removed_elems = true) const;


    template <typename I>
    uint32_t count_removed_elements_in_range(const array_iterator_t<I>& start, const array_iterator_t<I>& end) const
    {
        const long long N = (uint32_t)(end - start);
        MCUT_ASSERT(N >= 0);
        if (N == 0) {
            return 0;
        }


        const uint32_t start_ = (std::uint32_t)(start - elements_begin_(id_<array_iterator_t<I>> {}, false));
        uint32_t n = 0;

        for (auto elem_descr : get_removed_elements(id_<array_iterator_t<I>> {})) {
            const uint32_t descr = (uint32_t)elem_descr;

            if (descr >= start_ && (descr <= (start_ + (uint32_t)(N - 1)))) {
                ++n;
            }
        }
        return n;
    }

    const vec3& vertex(const vertex_descriptor_t& vd) const;

    const std::vector<halfedge_descriptor_t>& get_halfedges_around_vertex(const vertex_descriptor_t v) const;
    std::vector<vertex_descriptor_t> get_vertices_around_face(const face_descriptor_t f, uint32_t prepend_offset = 0) const;
    void get_vertices_around_face(std::vector<vertex_descriptor_t>& vertex_descriptors, const face_descriptor_t f, uint32_t prepend_offset=0) const;
    std::vector<vertex_descriptor_t> get_vertices_around_vertex(const vertex_descriptor_t v) const;
    void get_vertices_around_vertex(std::vector<vertex_descriptor_t>& vertices_around_vertex, const vertex_descriptor_t v) const;
    uint32_t get_num_vertices_around_face(const face_descriptor_t f) const;
    const std::vector<halfedge_descriptor_t>& get_halfedges_around_face(const face_descriptor_t f) const;
    const std::vector<face_descriptor_t> get_faces_around_face(const face_descriptor_t f, const std::vector<halfedge_descriptor_t>* halfedges_around_face_ = nullptr) const;
    void get_faces_around_face( std::vector<face_descriptor_t>& faces_around_face, const face_descriptor_t f, const std::vector<halfedge_descriptor_t>* halfedges_around_face_ = nullptr) const;
    uint32_t get_num_faces_around_face(const face_descriptor_t f, const std::vector<halfedge_descriptor_t>* halfedges_around_face_ = nullptr) const;




    vertex_array_iterator_t vertices_begin(bool account_for_removed_elems = true) const;
    vertex_array_iterator_t vertices_end() const;
    edge_array_iterator_t edges_begin(bool account_for_removed_elems = true) const;
    edge_array_iterator_t edges_end() const;
    halfedge_array_iterator_t halfedges_begin(bool account_for_removed_elems = true) const;
    halfedge_array_iterator_t halfedges_end() const;
    face_array_iterator_t faces_begin(bool account_for_removed_elems = true) const;
    face_array_iterator_t faces_end() const;

    const std::vector<vertex_descriptor_t>& get_removed_vertices() const;
    const std::vector<edge_descriptor_t>& get_removed_edges() const;
    const std::vector<halfedge_descriptor_t>& get_removed_halfedges() const;
    const std::vector<face_descriptor_t>& get_removed_faces() const;

private:



    std::vector<vertex_data_t> m_vertices;
    std::vector<edge_data_t> m_edges;
    std::vector<halfedge_data_t> m_halfedges;
    std::vector<face_data_t> m_faces;





    std::vector<face_descriptor_t> m_faces_removed;
    std::vector<edge_descriptor_t> m_edges_removed;
    std::vector<halfedge_descriptor_t> m_halfedges_removed;
    std::vector<vertex_descriptor_t> m_vertices_removed;

};

typedef vertex_descriptor_t vd_t;
typedef halfedge_descriptor_t hd_t;
typedef edge_descriptor_t ed_t;
typedef face_descriptor_t fd_t;

void write_off(const char* fpath, const hmesh_t& mesh);
void read_off(hmesh_t& mesh, const char* fpath);

template <typename V = face_array_t>
class array_iterator_t : public V::const_iterator {
    const hmesh_t* mesh_ptr;
    typedef typename V::const_iterator std_iterator_base_class;
    typedef typename V::value_type::type element_descriptor_type;
    typename V::value_type* operator->() = delete;

public:
    array_iterator_t()
        : V::const_iterator()
        , mesh_ptr(nullptr) {};
    array_iterator_t(typename V::const_iterator it_, const hmesh_t* const mesh)
        : V::const_iterator(it_)
        , mesh_ptr(mesh)
    {
    }

    const hmesh_t* get_mesh_ptr() const
    {
        return mesh_ptr;
    }

    typename V::value_type::type operator*()
    {
        size_t raw_index = (*this) - cbegin<>(false);
        element_descriptor_type d((std::uint32_t)raw_index);
        return d;
    }



    array_iterator_t<V>& operator++()
    {
        bool cur_elem_is_removed = false;
        bool reached_end = false;
        do {
            V::const_iterator::operator++();
            reached_end = (*this) == cend<>();
            cur_elem_is_removed = false;

            if (!reached_end) {
                const std::size_t diff = ((*this) - cbegin<array_iterator_t<V>>(false));
                element_descriptor_type raw_descriptor((std::uint32_t)diff);
                cur_elem_is_removed = mesh_ptr->is_removed(raw_descriptor);
                if (!cur_elem_is_removed) {
                    break;
                }
            }




        } while (cur_elem_is_removed && !reached_end);

        return (*this);
    }



    array_iterator_t<V>& operator+=(std::ptrdiff_t n)
    {
        V::const_iterator::operator+=(n);

        bool cur_elem_is_removed = false;
        bool reached_end = (*this) == cend<>();
        cur_elem_is_removed = mesh_ptr->is_removed(*(*this));
        while (!reached_end && cur_elem_is_removed) {
            V::const_iterator::operator++();
            size_t raw_descriptor = *(*this);
            cur_elem_is_removed = mesh_ptr->is_removed(element_descriptor_type((std::uint32_t)raw_descriptor));
            if (!cur_elem_is_removed) {
                break;
            }

            reached_end = (*this) == cend<>();
        }
        return *this;
    }







    template <typename I = array_iterator_t<V>>
    I cend()
    {
        return cend(id_<I>());
    }

    template <typename I = array_iterator_t<V>>
    I cbegin(bool account_for_removed_elems)
    {
        return cbegin(account_for_removed_elems, id_<I>());
    }

private:


    array_iterator_t<V> operator++(int)
    {
        MCUT_ASSERT(false);
        return cend<>();
    }

    template <typename I = array_iterator_t<V>>
    I cend(id_<I>)
    {
        return I();
    }

    template <typename I = array_iterator_t<V>>
    I cbegin(bool account_for_removed_elems, id_<I>)
    {
        return I(account_for_removed_elems);
    }

    vertex_array_iterator_t cbegin(bool account_for_removed_elems, id_<vertex_array_iterator_t> = {});
    vertex_array_iterator_t cend(id_<vertex_array_iterator_t>);

    edge_array_iterator_t cbegin(bool account_for_removed_elems, id_<edge_array_iterator_t> = {});
    edge_array_iterator_t cend(id_<edge_array_iterator_t>);

    halfedge_array_iterator_t cbegin(bool account_for_removed_elems, id_<halfedge_array_iterator_t> = {});
    halfedge_array_iterator_t cend(id_<halfedge_array_iterator_t>);

    face_array_iterator_t cbegin(bool account_for_removed_elems, id_<face_array_iterator_t> = {});
    face_array_iterator_t cend(id_<face_array_iterator_t>);
};

namespace std {
#if 1
template <>
inline typename edge_array_iterator_t::difference_type distance(
    edge_array_iterator_t first,
    edge_array_iterator_t last)
{
    MCUT_ASSERT(first.get_mesh_ptr() == last.get_mesh_ptr());
    edge_array_iterator_t it = first;
    edge_array_iterator_t::difference_type dist = last - first;

    uint32_t r = it.get_mesh_ptr()->count_removed_elements_in_range(first, last);
    if (r > 0) {
        dist = dist - r;
    }

    MCUT_ASSERT(dist >= 0);

    return dist;
}
#endif
#if 0
    template <>
    void advance(
        hmesh_t::array_iterator_t<hmesh_t::edge_array_t> &iter,
        typename std::iterator_traits<hmesh_t::array_iterator_t<hmesh_t::edge_array_t>>::difference_type n);
#endif

template <>
struct hash<vertex_descriptor_t> {
    std::size_t operator()(const vertex_descriptor_t& k) const
    {
        return std::hash<typename vertex_descriptor_t::index_type>()(static_cast<typename vertex_descriptor_t::index_type>(k));
    }
};

template <>
struct hash<edge_descriptor_t> {
    std::size_t operator()(const edge_descriptor_t& k) const
    {
        return std::hash<typename edge_descriptor_t::index_type>()(static_cast<typename edge_descriptor_t::index_type>(k));
    }
};

template <>
struct hash<halfedge_descriptor_t> {
    std::size_t operator()(const halfedge_descriptor_t& k) const
    {
        return std::hash<typename halfedge_descriptor_t::index_type>()(static_cast<typename halfedge_descriptor_t::index_type>(k));
    }
};

template <>
struct hash<face_descriptor_t> {
    std::size_t operator()(const face_descriptor_t& k) const
    {
        return std::hash<typename face_descriptor_t::index_type>()(static_cast<typename face_descriptor_t::index_type>(k));
    }
};
}

#endif
