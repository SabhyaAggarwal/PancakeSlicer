// based on MSH reader from PyMesh

// Copyright (c) 2015 Qingnan Zhou <qzhou@adobe.com>
// Copyright (C) 2020 Vladimir Fonov <vladimir.fonov@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MSH_LOADER_H
#define IGL_MSH_LOADER_H
#include "igl_inline.h"

#include <fstream>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

namespace igl {



class MshLoader {
    public:

        struct msh_struct {
            int tag,el_type;
            msh_struct(int _tag=0,int _type=0):
                tag(_tag),el_type(_type){}
            bool operator== (const msh_struct& a) const {
                return this->tag==a.tag &&
                       this->el_type==a.el_type;
            }

            bool operator< (const msh_struct& a) const {
                return (this->tag*100+this->el_type) <
                       (a.tag*100+a.el_type);
            }
        };

        typedef double Float;

        typedef std::vector<int>      IndexVector;
        typedef std::vector<int>      IntVector;
        typedef std::vector<Float>    FloatVector;
        typedef std::vector<FloatVector> FloatField;
        typedef std::vector<IntVector> IntField;
        typedef std::vector<std::string> FieldNames;
        typedef std::multimap<msh_struct,int> StructIndex;
        typedef std::vector<msh_struct> StructVector;

        enum {ELEMENT_LINE=1, ELEMENT_TRI=2, ELEMENT_QUAD=3,
              ELEMENT_TET=4,  ELEMENT_HEX=5, ELEMENT_PRISM=6,
              ELEMENT_PYRAMID=7,

              ELEMENT_LINE_2ND_ORDER=8, ELEMENT_TRI_2ND_ORDER=9,
              ELEMENT_QUAD_2ND_ORDER=10,ELEMENT_TET_2ND_ORDER=11,
              ELEMENT_HEX_2ND_ORDER=12, ELEMENT_PRISM_2ND_ORDER=13,
              ELEMENT_PYRAMID_2ND_ORDER=14,

              ELEMENT_POINT=15 };
    public:


        MshLoader(const std::string &filename);

    public:


        const FloatVector& get_nodes()    const { return m_nodes; }


        const IndexVector& get_elements() const { return m_elements; }


        const IntVector& get_elements_types() const { return m_elements_types; }

        const IntVector& get_elements_lengths() const { return m_elements_lengths; }

        const IntField&  get_elements_tags() const { return m_elements_tags; }

        const IntVector& get_elements_ids() const { return m_elements_ids; }


        const IndexVector& get_elements_nodes_idx() const { return m_elements_nodes_idx; }


        const FloatField& get_node_fields() const { return m_node_fields;}

        const FieldNames& get_node_fields_names() const { return m_node_fields_names;}

        const IntVector&  get_node_fields_components() const {return m_node_fields_components;}

        int get_node_field_components(size_t c)  const
        {
            return m_node_fields_components[c];
        }


        const FloatField& get_element_fields() const { return m_element_fields;}

        const FieldNames& get_element_fields_names() const { return m_element_fields_names;}

        const IntVector&  get_element_fields_components() const {return m_element_fields_components;}

        int get_element_field_components(size_t c)  const {
            return m_element_fields_components[c];
        }

        bool is_node_field(const std::string& fieldname)  const {
            return (std::find(std::begin(m_node_fields_names),
                              std::end(m_node_fields_names),
                              fieldname) != std::end(m_node_fields_names) );
        }

        bool is_element_field(const std::string& fieldname) const {
            return (std::find(std::begin(m_element_fields_names),
                              std::end(m_element_fields_names),
                              fieldname) != std::end(m_node_fields_names) );
        }


        bool is_element_map_identity() const ;



        void index_structures(int tag_column);


        const StructIndex& get_structure_index() const
        {
            return m_structure_index;
        }


        const StructIndex& get_structure_length() const
        {
            return m_structure_length;
        }


        const StructVector& get_structures() const
        {
            return m_structures;
        }

    public:

        static int num_nodes_per_elem_type(int elem_type);

    private:
        void parse_nodes(std::ifstream& fin);
        void parse_elements(std::ifstream& fin);
        void parse_node_field(std::ifstream& fin);
        void parse_element_field(std::ifstream& fin);
        void parse_unknown_field(std::ifstream& fin,
                const std::string& fieldname);

    private:
        bool   m_binary;
        size_t m_data_size;

        FloatVector m_nodes;

        IndexVector m_elements;
        IndexVector m_elements_nodes_idx;

        IntVector   m_elements_ids;
        IntVector   m_elements_types;
        IntVector   m_elements_lengths;
        IntField    m_elements_tags;

        FloatField  m_node_fields;
        IntVector   m_node_fields_components;
        FieldNames  m_node_fields_names;

        FloatField  m_element_fields;
        IntVector   m_element_fields_components;
        FieldNames  m_element_fields_names;

        StructIndex  m_structure_index;
        StructVector m_structures;
        StructIndex  m_structure_length;
};

}

#ifndef IGL_STATIC_LIBRARY
#  include "MshLoader.cpp"
#endif

#endif
