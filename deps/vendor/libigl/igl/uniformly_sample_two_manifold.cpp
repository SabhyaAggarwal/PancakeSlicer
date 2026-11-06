// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "uniformly_sample_two_manifold.h"
#include "verbose.h"
#include "colon.h"
#include "all_pairs_distances.h"
#include "vertex_triangle_adjacency.h"
#include "get_seconds.h"
#include "cat.h"
#include "placeholders.h"

#include "partition.h"





IGL_INLINE void igl::uniformly_sample_two_manifold(
  const Eigen::MatrixXd & W,
  const Eigen::MatrixXi & F,
  const int k,
  const double push,
  Eigen::MatrixXd & WS)
{
  using namespace Eigen;
  using namespace std;











  const auto & bary_dist = [] (
    const Eigen::MatrixXd & W,
    const Eigen::MatrixXi & F,
    const int face_A,
    const Eigen::Vector3d & bary_A,
    const int face_B,
    const Eigen::Vector3d & bary_B) -> double
  {
    return
      ((bary_A(0)*W.row(F(face_A,0)) +
        bary_A(1)*W.row(F(face_A,1)) +
        bary_A(2)*W.row(F(face_A,2)))
        -
        (bary_B(0)*W.row(F(face_B,0)) +
        bary_B(1)*W.row(F(face_B,1)) +
        bary_B(2)*W.row(F(face_B,2)))).norm();
  };



  if(F.cols() == 4)
  {
    verbose("uniform_sample.h: sampling tet mesh\n");
    MatrixXi T0 = F.col(0);
    MatrixXi T1 = F.col(1);
    MatrixXi T2 = F.col(2);
    MatrixXi T3 = F.col(3);

    MatrixXi TF =
      cat(1,
        cat(1,
          cat(2,T0, cat(2,T1,T2)),
          cat(2,T0, cat(2,T2,T3))),
        cat(1,
          cat(2,T0, cat(2,T3,T1)),
          cat(2,T1, cat(2,T3,T2)))
      );
    assert(TF.rows() == 4*F.rows());
    assert(TF.cols() == 3);
    uniformly_sample_two_manifold(W,TF,k,push,WS);
    return;
  }

  double start = get_seconds();

  VectorXi S;

  uniformly_sample_two_manifold_at_vertices(W,k,push,S);
  verbose("Lap: %g\n",get_seconds()-start);
  WS = W(S,igl::placeholders::all);







  vector<vector<int> > VF,VFi;
  vertex_triangle_adjacency(W,F,VF,VFi);


  vector<vector<int> > sample_faces; sample_faces.resize(k);


  vector<vector<Eigen::Vector3d> > sample_barys; sample_barys.resize(k);

  vector<int> cur_maxmin; cur_maxmin.resize(k);


  vector<MatrixXd> D; D.resize(k);


  MatrixXd I(MatrixXd::Identity(W.cols(),W.cols()));


  for(int i = 0;i < k;i++)
  {

    assert(VF[S(i)].size() > 0);
    sample_faces[i].push_back(VF[S(i)][0]);


    Eigen::Vector3d bary(0,0,0);
    bary( VFi[S(i)][0] ) = 1;
    sample_barys[i].push_back(bary);

    cur_maxmin[i] = 0;
  }


  double radius = 1.0;


  double min_radius = 1e-5;
  int max_num_rand_samples_per_triangle = 100;
  int max_sample_attempts_per_triangle = 1000;

  int max_iters = 1000;


  while(radius > min_radius)
  {

    for(int i = 0;i < k;i++)
    {

      int face_i = sample_faces[i][cur_maxmin[i]];
      Eigen::Vector3d bary(sample_barys[i][cur_maxmin[i]]);

      int index_in_face =
        (bary(0) > bary(1) ? (bary(0) > bary(2) ? 0 : 2)
                           : (bary(1) > bary(2) ? 1 : 2));

      int vertex_i = F(face_i,index_in_face);

      vector<int> incident_F = VF[vertex_i];


      sample_barys[i].clear();
      sample_faces[i].clear();
      cur_maxmin[i] = 0;
      sample_barys[i].push_back(bary);
      sample_faces[i].push_back(face_i);

      VectorXd seed =
        bary(0)*W.row(F(face_i,0)) +
        bary(1)*W.row(F(face_i,1)) +
        bary(2)*W.row(F(face_i,2));
#ifdef EXTREME_VERBOSE
      verbose("i: %d\n",i);
      verbose("face_i: %d\n",face_i);

      verbose("index_in_face: %d\n",index_in_face);
      verbose("vertex_i: %d\n",vertex_i);
      verbose("incident_F.size(): %d\n",incident_F.size());

#endif

      for(int f=0;f<(int)incident_F.size();f++)
      {
#ifdef EXTREME_VERBOSE
        verbose("incident_F[%d]: %d\n",f,incident_F[f]);
#endif
        int face_f = incident_F[f];
        int num_samples_f = 0;
        for(int s=0;s<max_sample_attempts_per_triangle;s++)
        {

          double u,v;


          double ru = (double)rand() / RAND_MAX;
          double rv = (double)rand() / RAND_MAX;

          if((ru+rv)>1)
          {
            u = 1-rv;
            v = 1-ru;
          }else
          {
            u = ru;
            v = rv;
          }
          Eigen::Vector3d sample_bary(u,v,1-u-v);
          double d = bary_dist(W,F,face_i,bary,face_f,sample_bary);

          if(d<radius)
          {

            sample_faces[i].push_back(face_f);
            sample_barys[i].push_back(sample_bary);
            num_samples_f++;
          }

          if(num_samples_f >= max_num_rand_samples_per_triangle)
          {
#ifdef EXTREME_VERBOSE
            verbose("Reached maximum number of samples per face\n");
#endif
            break;
          }
          if(s==(max_sample_attempts_per_triangle-1))
          {
#ifdef EXTREME_VERBOSE
            verbose("Reached maximum sample attempts per triangle\n");
#endif
          }
        }
#ifdef EXTREME_VERBOSE
        verbose("sample_faces[%d].size(): %d\n",i,sample_faces[i].size());
        verbose("sample_barys[%d].size(): %d\n",i,sample_barys[i].size());
#endif
      }
    }






    for(int i = 0;i < k;i++)
    {

      D[i].resize(sample_faces[i].size(),k+W.cols());

      for(int s = 0;s<(int)sample_faces[i].size();s++)
      {
        int sample_face = sample_faces[i][s];
        Eigen::Vector3d sample_bary = sample_barys[i][s];

        for(int j = 0;j < k;j++)
        {

          double d;
          if(i==j)
          {

            d = 10;
          }else
          {
            int seed_j_face = sample_faces[j][cur_maxmin[j]];
            Eigen::Vector3d seed_j_bary(sample_barys[j][cur_maxmin[j]]);
            d = bary_dist(W,F,sample_face,sample_bary,seed_j_face,seed_j_bary);
          }
          D[i](s,j) = d;
        }

        for(int j = 0;j < W.cols();j++)
        {

          double d =
            ((sample_bary(0)*W.row(F(sample_face,0)) +
              sample_bary(1)*W.row(F(sample_face,1)) +
              sample_bary(2)*W.row(F(sample_face,2)))
              - I.row(j)).norm()/push;

          D[i](s,k+j) = d;
        }
      }
    }

    int iters = 0;
    while(true)
    {
      bool has_changed = false;

      for(int i = 0;i < k;i++)
      {

        VectorXd minD = D[i].rowwise().minCoeff();
        assert(minD.size() == (int)sample_faces[i].size());

        int old_cur_maxmin = cur_maxmin[i];
        double max_min = -2;
        for(int s = 0;s<(int)sample_faces[i].size();s++)
        {
          if(max_min < minD(s))
          {
            max_min = minD(s);

            cur_maxmin[i] = s;
          }
        }
#ifdef EXTREME_VERBOSE
        verbose("max_min: %g\n",max_min);
        verbose("cur_maxmin[%d]: %d->%d\n",i,old_cur_maxmin,cur_maxmin[i]);
#endif

        has_changed |= (old_cur_maxmin!=cur_maxmin[i]);

      }

      if(!has_changed)
      {
        break;
      }
      iters++;
      if(iters>=max_iters)
      {
        verbose("Hit max iters (%d) before converging\n",iters);
      }
    }



    radius *= 0.9;
  }

  WS.resize(k,W.cols());
  for(int i = 0;i<k;i++)
  {
    int face_i = sample_faces[i][cur_maxmin[i]];
    Eigen::Vector3d bary(sample_barys[i][cur_maxmin[i]]);
    WS.row(i) =
        bary(0)*W.row(F(face_i,0)) +
        bary(1)*W.row(F(face_i,1)) +
        bary(2)*W.row(F(face_i,2));
  }
  verbose("Lap: %g\n",get_seconds()-start);

}

IGL_INLINE void igl::uniformly_sample_two_manifold_at_vertices(
  const Eigen::MatrixXd & OW,
  const int k,
  const double push,
  Eigen::VectorXi & S)
{
  using namespace Eigen;
  using namespace std;


  const MatrixXd & W = OW;



  VectorXi G;
  Matrix<double,Dynamic,1>  ignore;
  partition(W,k+W.cols(),G,S,ignore);

  S = S.segment(W.cols(),k).eval();

  MatrixXd WS = W(S,igl::placeholders::all);



  int n = W.rows();

  int m = W.cols();

  MatrixXd I = MatrixXd::Identity(m,m);

  MatrixXd WI(n+m,m);
  WI << W,I;

  MatrixXd WSC(k+m,m);
  for(int i = 0;i<k;i++)
  {
    WSC.row(i) = W.row(S(i));
  }
  for(int i = 0;i<m;i++)
  {
    WSC.row(i+k) = WI.row(n+i);
  }

  MatrixXd sqrD;
  all_pairs_distances(WI,WSC,true,sqrD);

  sqrD.block(0,k,sqrD.rows(),m) /= push*push;

  int max_iters = 30;
  int j = 0;
  for(;j<max_iters;j++)
  {
    bool has_changed = false;

    for(int i =0;i<k;i++)
    {
      int old_si = S(i);

      sqrD.col(i).setZero();
      sqrD.col(i).array() += 10;

      MatrixXd minsqrD = sqrD.rowwise().minCoeff();
      MatrixXd::Index si,PHONY;
      minsqrD.maxCoeff(&si,&PHONY);
      MatrixXd Wsi = W.row(si);
      MatrixXd sqrDi;
      all_pairs_distances(WI,Wsi,true,sqrDi);
      sqrD.col(i) = sqrDi;
      S(i) = si;
      has_changed |= si!=old_si;
    }
    if(j == max_iters)
    {
      verbose("uniform_sample.h: Warning: hit max iters\n");
    }
    if(!has_changed)
    {
      break;
    }
  }
}
