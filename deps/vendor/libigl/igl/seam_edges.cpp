// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Yotam Gingold <yotam@yotamgingold.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.

#include "seam_edges.h"
#include <unordered_map>
#include <unordered_set>
#include <cassert>



template <
  typename DerivedV,
  typename DerivedTC,
  typename DerivedF,
  typename DerivedFTC,
  typename Derivedseams,
  typename Derivedboundaries,
  typename Derivedfoldovers>
IGL_INLINE void igl::seam_edges(
  const Eigen::MatrixBase<DerivedV>& V,
  const Eigen::MatrixBase<DerivedTC>& TC,
  const Eigen::MatrixBase<DerivedF>& F,
  const Eigen::MatrixBase<DerivedFTC>& FTC,
  Eigen::PlainObjectBase<Derivedseams>& seams,
  Eigen::PlainObjectBase<Derivedboundaries>& boundaries,
  Eigen::PlainObjectBase<Derivedfoldovers>& foldovers)
{

  assert( F.cols() == 3 );
  assert( F.cols() == FTC.cols() );
  assert( F.rows() == FTC.rows() );


  assert( TC.cols() == 2 );
  typedef Eigen::Matrix< typename DerivedTC::Scalar, 2, 1 > Vector2S;



  const auto& Orientation = [](
    const Vector2S& a,
    const Vector2S& b,
    const Vector2S& c ) -> typename DerivedTC::Scalar
  {
      const Vector2S row0 = a - c;
      const Vector2S row1 = b - c;
      return row0(0)*row1(1) - row1(0)*row0(1);
  };

  seams     .setZero( 3*F.rows(), 4 );
  boundaries.setZero( 3*F.rows(), 2 );
  foldovers .setZero( 3*F.rows(), 4 );

  int num_seams = 0;
  int num_boundaries = 0;
  int num_foldovers = 0;













  typedef std::pair< typename DerivedF::Scalar, typename DerivedF::Scalar >
    directed_edge;
	const int numV = V.rows();
	const int numF = F.rows();
	const auto& edge_hasher =
    [numV]( directed_edge const& e ) { return e.first*numV + e.second; };



	std::unordered_map<directed_edge,std::pair<int,int>,decltype(edge_hasher) >
    directed_position_edge2face_position_index(2*( numV + numF ), edge_hasher);
  for( int fi = 0; fi < F.rows(); ++fi )
  {
    for( int i = 0; i < 3; ++i )
    {
      const int j = ( i+1 ) % 3;
      directed_position_edge2face_position_index[
        std::make_pair( F(fi,i), F(fi,j) ) ] = std::make_pair( fi, i );
    }
  }



  std::unordered_set< directed_edge, decltype( edge_hasher ) >
    undirected_position_edges( numV + numF, edge_hasher );
  for( const auto& el : directed_position_edge2face_position_index )
  {


    undirected_position_edges.insert( std::make_pair(
      std::min( el.first.first, el.first.second ),
      std::max( el.first.first, el.first.second ) ) );
  }





  for( const auto& vp_edge : undirected_position_edges )
  {


    assert( vp_edge.first < vp_edge.second );

    const auto vp_edge_reverse = std::make_pair(vp_edge.second, vp_edge.first);


    if( directed_position_edge2face_position_index.count( vp_edge ) &&
      directed_position_edge2face_position_index.count( vp_edge_reverse ) )
    {
      const auto forwards =
        directed_position_edge2face_position_index[ vp_edge ];
      const auto backwards =
        directed_position_edge2face_position_index[ vp_edge_reverse ];


      assert( forwards != backwards );



      if(
        std::make_pair(
          FTC( forwards.first, forwards.second ),
          FTC( forwards.first, ( forwards.second+1 ) % 3 ) )
        ==
        std::make_pair(
          FTC( backwards.first, ( backwards.second+1 ) % 3 ),
          FTC( backwards.first, backwards.second ) ))
      {



        const Vector2S a = TC.row( FTC( forwards.first,  forwards.second ) );
        const Vector2S b =
          TC.row( FTC( forwards.first, (forwards.second+1) % 3 ) );
        const Vector2S c_forwards  =
          TC.row( FTC( forwards .first, (forwards .second+2) % 3 ) );
        const Vector2S c_backwards =
          TC.row( FTC( backwards.first, (backwards.second+2) % 3 ) );


        const auto orientation_forwards = Orientation( a, b, c_forwards );
        const auto orientation_backwards = Orientation( a, b, c_backwards );
        if( ( orientation_forwards > 0 && orientation_backwards > 0 ) ||
            ( orientation_forwards < 0 && orientation_backwards < 0 )
            ) {
            foldovers( num_foldovers, 0 ) = forwards.first;
            foldovers( num_foldovers, 1 ) = forwards.second;
            foldovers( num_foldovers, 2 ) = backwards.first;
            foldovers( num_foldovers, 3 ) = backwards.second;
            num_foldovers += 1;
        }
      }

      else
      {
        seams( num_seams, 0 ) = forwards.first;
        seams( num_seams, 1 ) = forwards.second;
        seams( num_seams, 2 ) = backwards.first;
        seams( num_seams, 3 ) = backwards.second;
        num_seams += 1;
      }
    }


    else if( directed_position_edge2face_position_index.count( vp_edge ) )
    {
      const auto forwards = directed_position_edge2face_position_index[vp_edge];
      boundaries( num_boundaries, 0 ) = forwards.first;
      boundaries( num_boundaries, 1 ) = forwards.second;
      num_boundaries += 1;
    } else if(
      directed_position_edge2face_position_index.count( vp_edge_reverse ) )
    {
      const auto backwards =
        directed_position_edge2face_position_index[ vp_edge_reverse ];
      boundaries( num_boundaries, 0 ) = backwards.first;
      boundaries( num_boundaries, 1 ) = backwards.second;
      num_boundaries += 1;
    } else {

      assert(
        directed_position_edge2face_position_index.count( vp_edge ) ||
        directed_position_edge2face_position_index.count( vp_edge_reverse )
      );
    }
  }

  seams     .conservativeResize( num_seams,      Eigen::NoChange_t() );
  boundaries.conservativeResize( num_boundaries, Eigen::NoChange_t() );
  foldovers .conservativeResize( num_foldovers,  Eigen::NoChange_t() );
}

#ifdef IGL_STATIC_LIBRARY


template void igl::seam_edges<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#endif
