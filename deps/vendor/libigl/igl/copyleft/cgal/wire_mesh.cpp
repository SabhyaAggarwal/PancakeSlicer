#include "wire_mesh.h"

#include "../../list_to_matrix.h"
#include "../../PI.h"
#include "../../placeholders.h"
#include "convex_hull.h"
#include "coplanar.h"
#include "mesh_boolean.h"
#include <Eigen/Geometry>
#include <vector>

template <
  typename DerivedWV,
  typename DerivedWE,
  typename Derivedth,
  typename DerivedV,
  typename DerivedF,
  typename DerivedJ>
IGL_INLINE void igl::copyleft::cgal::wire_mesh(
  const Eigen::MatrixBase<DerivedWV> & WV,
  const Eigen::MatrixBase<DerivedWE> & WE,
  const Eigen::MatrixBase<Derivedth> & th,
  const int poly_size,
  const bool solid,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  assert((th.size()==1 || th.size()==WE.rows()) &&
    "th should be scalar or size of WE");

  typedef typename DerivedWV::Scalar Scalar;

  typedef Eigen::Matrix<Scalar,Eigen::Dynamic,3> MatrixX3S;
  MatrixX3S PV(poly_size,3);
  for(int p =0;p<PV.rows();p++)
  {
    const Scalar phi = (Scalar(p)/Scalar(PV.rows()))*2.*igl::PI;
    PV(p,0) = 0.5*cos(phi);
    PV(p,1) = 0.5*sin(phi);
    PV(p,2) = 0;
  }

  V.resize(WV.rows() + PV.rows() * 2 * WE.rows(),3);
  V.topLeftCorner(WV.rows(),3) = WV;

  std::vector<std::vector<std::pair<int,int> > > A(WV.rows());





  const auto index =
    [&PV,&WV](const int e, const int c, const int p)->int
  {
    return WV.rows() + e*2*PV.rows() + PV.rows()*c + p;
  };













  std::vector<int> nedges(WV.rows(), 0);
  for(int e = 0;e<WE.rows();e++)
  {
    ++nedges[WE(e, 0)];
    ++nedges[WE(e, 1)];
  }


  for(int e = 0;e<WE.rows();e++)
  {

    A[WE(e,0)].emplace_back(e,0);
    A[WE(e,1)].emplace_back(e,1);
    typedef Eigen::Matrix<Scalar,1,3> RowVector3S;
    const RowVector3S ev = WV.row(WE(e,1))-WV.row(WE(e,0));
    const Scalar len = ev.norm();

    const RowVector3S uv = ev.normalized();
    Eigen::Quaternion<Scalar> q;
    q = q.FromTwoVectors(RowVector3S(0,0,1),uv);

    for(int p = 0;p<PV.rows();p++)
    {
      RowVector3S qp = q*(PV.row(p)*th(e%th.size()));

      for(int c = 0;c<2;c++)
      {

        const Scalar dir = c==0?1:-1;





        Scalar dist =
          std::min(1.*th(e%th.size()),len/3.0)*(nedges[WE(e,c)] > 1);

        V.row(index(e,c,p)) =
          qp+WV.row(WE(e,c)) + dist*dir*uv;
      }
    }
  }

  std::vector<std::vector<typename DerivedF::Index> > vF;
  std::vector<int> vJ;
  const auto append_hull =
    [&V,&vF,&vJ](const Eigen::VectorXi & I, const int j)
  {
    MatrixX3S Vv = V(I,igl::placeholders::all);

    if(coplanar(Vv))
    {
      return;
    }
    Eigen::MatrixXi Fv;
    convex_hull(Vv,Fv);
    for(int f = 0;f<Fv.rows();f++)
    {
      const Eigen::Array<int,1,3> face(I(Fv(f,0)), I(Fv(f,1)), I(Fv(f,2)));






























      vF.push_back( { face(0),face(1),face(2)});
      vJ.push_back(j);
    }
  };

  for(int v = 0;v<WV.rows();v++)
  {


    Eigen::VectorXi I(1+A[v].size()*PV.rows());

    I(0) = v;
    for(int n = 0;n<A[v].size();n++)
    {
      for(int p = 0;p<PV.rows();p++)
      {
        const int e = A[v][n].first;
        const int c = A[v][n].second;
        I(1+n*PV.rows()+p) = index(e,c,p);
      }
    }
    append_hull(I,v);
  }

  for(int e = 0;e<WE.rows();e++)
  {

    Eigen::VectorXi I(PV.rows()*2);
    for(int c = 0;c<2;c++)
    {
      for(int p = 0;p<PV.rows();p++)
      {
        I(c*PV.rows()+p) = index(e,c,p);
      }
    }
    append_hull(I,WV.rows()+e);
  }

  list_to_matrix(vF,F);
  if(solid)
  {

    igl::copyleft::cgal::mesh_boolean(
      Eigen::MatrixXd(V),Eigen::MatrixXi(F),Eigen::MatrixXd(),Eigen::MatrixXi(),
      "union",
      V,F,J);
    for(int j=0;j<J.size();j++) J(j) = vJ[J(j)];
  }else
  {
    list_to_matrix(vJ,J);
  }
}

template <
  typename DerivedWV,
  typename DerivedWE,
  typename DerivedV,
  typename DerivedF,
  typename DerivedJ>
IGL_INLINE void igl::copyleft::cgal::wire_mesh(
  const Eigen::MatrixBase<DerivedWV> & WV,
  const Eigen::MatrixBase<DerivedWE> & WE,
  const double th,
  const int poly_size,
  const bool solid,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  return wire_mesh(
    WV,WE,(Eigen::VectorXd(1,1)<<th).finished(),poly_size,solid,V,F,J);
}

template <
  typename DerivedWV,
  typename DerivedWE,
  typename DerivedV,
  typename DerivedF,
  typename DerivedJ>
IGL_INLINE void igl::copyleft::cgal::wire_mesh(
  const Eigen::MatrixBase<DerivedWV> & WV,
  const Eigen::MatrixBase<DerivedWE> & WE,
  const double th,
  const int poly_size,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedJ> & J)
{
  return wire_mesh(WV,WE,th,poly_size,true,V,F,J);
}

#ifdef IGL_STATIC_LIBRARY

template void igl::copyleft::cgal::wire_mesh<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, double, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
#endif
