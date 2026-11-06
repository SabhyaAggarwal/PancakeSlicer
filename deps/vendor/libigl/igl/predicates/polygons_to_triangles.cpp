#include "polygons_to_triangles.h"
#include "ear_clipping.h"
#include "../sort.h"
#include "../placeholders.h"
#include "../PlainMatrix.h"
#include <Eigen/Eigenvalues>

template <
  typename DerivedV,
  typename DerivedI,
  typename DerivedC,
  typename DerivedF,
  typename DerivedJ>
IGL_INLINE void igl::predicates::polygons_to_triangles(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedI> & I,
  const Eigen::MatrixBase<DerivedC> & C,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  typedef Eigen::Index Index;

  F.resize(C(C.size()-1) - (C.size()-1)*2,3);
  J.resize(F.rows());
  {
    Index f = 0;
    for(Index p = 0;p<C.size()-1;p++)
    {
      const Index np = C(p+1)-C(p);
      Eigen::MatrixXi pF;
      if(np == 3)
      {
        pF = (Eigen::MatrixXi(1,3)<<0,1,2).finished();
      }else
      {

        PlainMatrix<DerivedV,Eigen::Dynamic> pV(np,V.cols());
        for(Index c = 0;c<np;c++)
        {
          pV.row(c) = V.row(I(C(p)+c));
        }

        Eigen::MatrixXd S;
        switch(V.cols())
        {
          case 2:
            S = V.template cast<double>();
            break;
          case 3:
          {
            Eigen::MatrixXd P = (pV.rowwise() - pV.colwise().mean()).template cast<double>();
            Eigen::Matrix3d O = P.transpose() * P;
            Eigen::EigenSolver<Eigen::Matrix3d> es(O);
            Eigen::Matrix3d C = es.eigenvectors().real();
            {
              Eigen::Vector3d _1;
              Eigen::Vector3i I;
              igl::sort(es.eigenvalues().real().eval(),1,false,_1,I);
              C = C(igl::placeholders::all,I).eval();
            }
            S = P*C.leftCols(2);
            break;
          }
          default: assert(false && "dim>3 not supported");
        }

        Eigen::VectorXi RT = Eigen::VectorXi::Zero(S.rows(),1);
        Eigen::VectorXi _I;
        Eigen::MatrixXd _nS;


        {
          double area = 0;
          for(Index c = 0;c<np;c++)
          {
            area += S((c+0)%np,0)*S((c+1)%np,1) - S((c+1)%np,0)*S((c+0)%np,1);
          }

          if(area<0)
          {
            S.col(0) *= -1;
          }
        }






        if(! igl::predicates::ear_clipping(S,pF) )
        {








          pF.resize(np-2,3);
          for(Index c = 0;c<np;c++)
          {
            if(c>0 && c<np-1)
            {
              pF(c-1,0) = 0;
              pF(c-1,1) = c;
              pF(c-1,2) = c+1;
            }
          }
        }
        assert(pF.rows() == np-2);






































         \














































































      }

      for(Index i = 0;i<pF.rows();i++)
      {
        for(Index c =0;c<3;c++)
        {
          F(f,c) = I(C(p)+pF(i,c));
        }
        J(f) = p;
        f++;
      }

    }
    assert(f == F.rows());
  }
}

#ifdef IGL_STATIC_LIBRARY

#endif
