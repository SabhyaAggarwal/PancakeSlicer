#include "dual_contouring.h"
#include "quadprog.h"
#include "parallel_for.h"
#include <thread>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstdint>

namespace igl
{

  class Hash
  {
    public:

      std::uint64_t operator()(const std::tuple<int, int, int> & key) const
      {



        assert( std::get<0>(key) == (int)(std::int16_t)std::get<0>(key));
        assert( std::get<1>(key) == (int)(std::int16_t)std::get<1>(key));
        assert( std::get<2>(key) == (int)(std::int16_t)std::get<2>(key));
        std::uint64_t result = std::uint16_t(std::get<0>(key));
        result = (result << 16) + std::uint16_t(std::get<1>(key));
        result = (result << 16) + std::uint16_t(std::get<2>(key));
        return result;
      };
  };
  template <typename Scalar>
  class DualContouring
  {

    public:
      using RowVector3S = Eigen::Matrix<Scalar,1,3>;
      using RowVector4S = Eigen::Matrix<Scalar,1,4>;
      using Matrix4S = Eigen::Matrix<Scalar,4,4>;
      using Matrix3S = Eigen::Matrix<Scalar,3,3>;
      using Vector3S = Eigen::Matrix<Scalar,3,1>;
      using KeyTriplet = std::tuple<int,int,int>;
    public:



      std::function<Scalar(const RowVector3S &)> f;


      std::function<RowVector3S(const RowVector3S &)> f_grad;
      bool constrained;
      bool triangles;
      bool root_finding;
      RowVector3S min_corner;
      RowVector3S step;
      Eigen::Matrix<Scalar,Eigen::Dynamic,3> V;


      typename decltype(V)::Index n;

      std::unordered_map< KeyTriplet, typename decltype(V)::Index, Hash > C2V;


      std::vector<RowVector3S,Eigen::aligned_allocator<RowVector3S>> vV;

      std::vector<Eigen::RowVector3i,Eigen::aligned_allocator<Eigen::RowVector3i>> vI;

      std::vector<Matrix4S,Eigen::aligned_allocator<Matrix4S>> vH;


      std::vector<int> vcount;

      Eigen::Matrix<Eigen::Index,Eigen::Dynamic,Eigen::Dynamic> Q;

      typename decltype(Q)::Index m;

      std::mutex Qmut;
      std::mutex Vmut;
    public:
      DualContouring(
        const std::function<Scalar(const RowVector3S &)> & _f,
        const std::function<RowVector3S(const RowVector3S &)> & _f_grad,
        const bool _constrained = false,
        const bool _triangles = false,
        const bool _root_finding = true):
        f(_f),
        f_grad(_f_grad),
        constrained(_constrained),
        triangles(_triangles),
        root_finding(_root_finding),
        n(0),
        C2V(0),
        vV(),vI(),vH(),vcount(),
        m(0)
      { }


      typename decltype(V)::Index new_vertex()
      {
        const auto v = n;
        n++;
        vcount.resize(n);
        vV.resize(n);
        vI.resize(n);
        vH.resize(n);
        vcount[v] = 0;
        vV[v].setZero();
        vH[v].setZero();
        return v;
      };




      typename decltype(V)::Index sub2dual(const Eigen::RowVector3i & kc)
      {
        const KeyTriplet key = {kc(0),kc(1),kc(2)};
        const auto it = C2V.find(key);
        typename decltype(V)::Index v = -1;
        if(it == C2V.end())
        {
          v = new_vertex();
          C2V[key] = v;
          vI[v] = kc;
        }else
        {
          v = it->second;
        }
        return v;
      };
      RowVector3S primal(const Eigen::RowVector3i & ic) const
      {
        return min_corner + (ic.cast<Scalar>().array() * step.array()).matrix();
      }
      Eigen::RowVector3i inverse_primal(const RowVector3S & x) const
      {




        return
          ((x-min_corner).array()/step.array()).round().template cast<int>();
      }







      bool single_edge(const int & x, const int & y, const int & z, const int & o)
      {
        const RowVector3S e0 = primal(Eigen::RowVector3i(x,y,z));
        const Scalar f0 = f(e0);
        return single_edge(x,y,z,o,e0,f0);
      }
      bool single_edge(
        const int & x,
        const int & y,
        const int & z,
        const int & o,
        const RowVector3S & e0,
        const Scalar & f0)
      {




        Eigen::RowVector3i jc(x,y,z);
        jc(o) -= 1;
        const RowVector3S e1 = primal(jc);
        const Scalar f1 = f(e1);
        return single_edge(x,y,z,o,e0,f0,e1,f1);
      }
      bool single_edge(
        const int & x,
        const int & y,
        const int & z,
        const int & o,
        const RowVector3S & e0,
        const Scalar & f0,
        const RowVector3S & e1,
        const Scalar & f1)
      {
        const Scalar isovalue = 0;
        if((f0>isovalue) == (f1>isovalue)) { return false; }

        RowVector3S p;
        Scalar t = -1;
        if(root_finding)
        {
          Scalar tl = 0;
          bool gl = f0>0;
          Scalar tu = 1;
          bool gu = f1>0;
          assert(gu ^ gl);
          int riter = 0;
          const int max_riter = 7;
          while(true)
          {
            t = 0.5*(tu + tl);
            p = e0+t*(e1-e0);
            riter++;
            if(riter > max_riter) { break;}
            const Scalar ft = f(p);
            if( (ft>0) == gu) { tu = t; }
            else if( (ft>0) == gl){ tl = t; }
            else { break; }
          }
        }else
        {

          const Scalar delta = f1-f0;
          if(delta == 0) { t = 0.5; }
          t = (isovalue - f0)/delta;
          p = e0+t*(e1-e0);
        }
        typename decltype(V)::Index ev;

        {
            const std::lock_guard<std::mutex> lock(Vmut);

            ev = triangles ? new_vertex() : -1;
            if (triangles)
            {
                vV[ev] = p;
                vcount[ev] = 1;
                vI[ev] = Eigen::RowVector3i(-1, -1, -1);
            }
        }


        const RowVector3S dfdx = f_grad(p);

        const RowVector4S P = (RowVector4S()<<dfdx,-dfdx.dot(p)).finished();

        const Matrix4S H = P.transpose() * P;

        Eigen::RowVector4i face;
        int k = 0;
        for(int i = -1;i<=0;i++)
        {
          for(int j = -1;j<=0;j++)
          {
            Eigen::RowVector3i kc(x,y,z);
            kc(o)--;
            kc((o+1)%3)+=i;
            kc((o+2)%3)+=j;
            const std::lock_guard<std::mutex> lock(Vmut);
            const typename decltype(V)::Index v = sub2dual(kc);
            vV[v] += p;
            vcount[v]++;
            vH[v] += H;
            face(k++) = v;
          }
        }
        {
          const std::lock_guard<std::mutex> lock(Qmut);
          if(triangles)
          {
            if(m+4 >= Q.rows()){ Q.conservativeResize(2*m+4,Q.cols()); }
            if(f0>f1)
            {
              Q.row(m+0)<<      ev,face(3),face(1)        ;
              Q.row(m+1)<<              ev,face(1),face(0);
              Q.row(m+2)<< face(2),             ev,face(0);
              Q.row(m+3)<< face(2),face(3),             ev;
            }else
            {
              Q.row(m+0)<<      ev,face(1),face(3)        ;
              Q.row(m+1)<<              ev,face(3),face(2);
              Q.row(m+2)<< face(0),             ev,face(2);
              Q.row(m+3)<< face(0),face(1),             ev;
            }
            m+=4;
          }else
          {
            if(m+1 >= Q.rows()){ Q.conservativeResize(2*m+1,Q.cols()); }
            if(f0>f1)
            {
              Q.row(m)<< face(2),face(3),face(1),face(0);
            }else
            {
              Q.row(m)<< face(0),face(1),face(3),face(2);
            }
            m++;
          }
        }
        return true;
      }


      void dual_vertex_positions()
      {
        Q.conservativeResize(m,Q.cols());
        V.resize(n,3);
        igl::parallel_for(n,[&](const Eigen::Index v)
        {
          RowVector3S mid = vV[v] / Scalar(vcount[v]);
          if(triangles && vI[v](0)<0 ){ V.row(v) = mid; return; }
          const Scalar w = 1e-2*(0.01+vcount[v]);
          Matrix3S A = vH[v].block(0,0,3,3) + w*Matrix3S::Identity();
          RowVector3S b = -vH[v].block(3,0,1,3) + w*mid;









          const RowVector3S p0 =
            min_corner + ((vI[v].template cast<Scalar>().array()) * step.array()).matrix();
          const RowVector3S x =
            constrained ?
            igl::quadprog<Scalar,3>(A,(p0*A-b).transpose(),Vector3S(0,0,0),step.transpose()) :
            Eigen::LLT<Matrix3S>(A).solve(-(p0*A-b).transpose());
          V.row(v) = p0+x;
        },1000ul);
      }







      void dense(
        const RowVector3S & _min_corner,
        const RowVector3S & max_corner,
        const int nx,
        const int ny,
        const int nz)
      {
        min_corner = _min_corner;
        step =
          (max_corner-min_corner).array()/(RowVector3S(nx,ny,nz).array()-1);

        Q.resize(std::pow(nx*ny*nz,2./3.),triangles?3:4);

        igl::parallel_for(nx,[&](const int x)
        {
          for(int y = 0;y<ny;y++)
          {
            for(int z = 0;z<nz;z++)
            {
              const RowVector3S e0 = primal(Eigen::RowVector3i(x,y,z));
              const Scalar f0 = f(e0);

              for(int o = 0;o<3;o++)
              {

                if(((o==0)&&x==0)||((o==1)&&y==0)||((o==2)&&z==0)){ continue;}
                single_edge(x,y,z,o,e0,f0);
              }
            }
          }
        },10ul);
        dual_vertex_positions();
      }
      template <typename DerivedGf, typename DerivedGV>
      void dense(
        const Eigen::MatrixBase<DerivedGf> & Gf,
        const Eigen::MatrixBase<DerivedGV> & GV,
        const int nx,
        const int ny,
        const int nz)
      {
        min_corner = GV.colwise().minCoeff();
        const RowVector3S max_corner = GV.colwise().maxCoeff();
        step =
          (max_corner-min_corner).array()/(RowVector3S(nx,ny,nz).array()-1);


        Q.resize(std::pow(nx*ny*nz,2./3.),triangles?3:4);

        const auto xyz2i = [&nx,&ny]
          (const int & x, const int & y, const int & z)->Eigen::Index
        {
          return x+nx*(y+ny*(z));
        };


        igl::parallel_for(nz,[&](const int z)
        {
          for(int y = 0;y<ny;y++)
          {
            for(int x = 0;x<nx;x++)
            {

              const Eigen::Index k0 = xyz2i(x,y,z);
              const RowVector3S e0 = GV.row(k0);
              const Scalar f0 = Gf(k0);

              for(int o = 0;o<3;o++)
              {
                Eigen::RowVector3i jc(x,y,z);
                jc(o) -= 1;
                if(jc(o)<0) { continue;}
                const int k1 = xyz2i(jc(0),jc(1),jc(2));
                const RowVector3S e1 = GV.row(k1);
                const Scalar f1 = Gf(k1);
                single_edge(x,y,z,o,e0,f0,e1,f1);
              }
            }
          }
        },10ul);
        dual_vertex_positions();
      }
      void sparse(
        const RowVector3S & _step,
        const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & Gf,
        const Eigen::Matrix<Scalar,Eigen::Dynamic,3> & GV,
        const Eigen::Matrix<int,Eigen::Dynamic,2> & GI)
      {
        step = _step;
        Q.resize((triangles?4:1)*GI.rows(),triangles?3:4);




        min_corner = GV.colwise().minCoeff();


        for(Eigen::Index i = 0;i<GI.rows();i++)
        {
          RowVector3S e0 = GV.row(GI(i,0));
          RowVector3S e1 = GV.row(GI(i,1));
          Eigen::RowVector3i ic0 = inverse_primal(e0);
          Eigen::RowVector3i ic1 = inverse_primal(e1);
#ifndef NDEBUG
          RowVector3S p0 = primal(ic0);
          RowVector3S p1 = primal(ic1);
          assert( (p0-e0).norm() < 1e-10);
          assert( (p1-e1).norm() < 1e-10);
#endif
          Scalar f0 = Gf(GI(i,0));
          Scalar f1 = Gf(GI(i,1));

          int o = -1;
          for(int j = 0;j<3;j++)
          {
            if(ic0(j) == ic1(j)){ continue;}
            if(ic0(j) - ic1(j) == 1)
            {
              assert(o == -1 && "Edges should differ in just one coordinate");
              o = j;
              continue;
            }
            if(ic1(j) - ic0(j) == 1)
            {
              assert(o == -1 && "Edges should differ in just one coordinate");
              std::swap(e0,e1);
              std::swap(f0,f1);
              std::swap(ic0,ic1);
              o = j;
              continue;
            } else
            {
              assert(false && "Edges should differ in just one coordinate");
            }
          }
          assert(o>=0 && "Edges should differ in just one coordinate");


          for(int j = 0;j<3;j++){ assert(ic0(j) == ic1(j)+(o==j)); }
          const int x = ic0(0);
          const int y = ic0(1);
          const int z = ic0(2);
          single_edge(x,y,z,o,e0,f0,e1,f1);
        }
        dual_vertex_positions();
      }
  };
}

template <
  typename DerivedV,
  typename DerivedQ>
IGL_INLINE void igl::dual_contouring(
  const std::function<
    typename DerivedV::Scalar(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f,
  const std::function<
    Eigen::Matrix<typename DerivedV::Scalar,1,3>(
      const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f_grad,
  const Eigen::Matrix<typename DerivedV::Scalar,1,3> & min_corner,
  const Eigen::Matrix<typename DerivedV::Scalar,1,3> & max_corner,
  const int nx,
  const int ny,
  const int nz,
  const bool constrained,
  const bool triangles,
  const bool root_finding,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedQ> & Q)
{
  typedef typename DerivedV::Scalar Scalar;
  DualContouring<Scalar> DC(f,f_grad,constrained,triangles,root_finding);
  DC.dense(min_corner,max_corner,nx,ny,nz);
  V = DC.V;
  Q = DC.Q.template cast<typename DerivedQ::Scalar>();
}

template <
  typename DerivedGf,
  typename DerivedGV,
  typename DerivedV,
  typename DerivedQ>
IGL_INLINE void igl::dual_contouring(
  const std::function<
    typename DerivedV::Scalar(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f,
  const std::function<
    Eigen::Matrix<typename DerivedV::Scalar,1,3>(
      const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f_grad,
  const Eigen::MatrixBase<DerivedGf> & Gf,
  const Eigen::MatrixBase<DerivedGV> & GV,
  const int nx,
  const int ny,
  const int nz,
  const bool constrained,
  const bool triangles,
  const bool root_finding,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedQ> & Q)
{
  typedef typename DerivedV::Scalar Scalar;
  DualContouring<Scalar> DC(f,f_grad,constrained,triangles,root_finding);
  DC.dense(Gf,GV,nx,ny,nz);
  V = DC.V;
  Q = DC.Q.template cast<typename DerivedQ::Scalar>();
}

template <
  typename DerivedGf,
  typename DerivedGV,
  typename DerivedGI,
  typename DerivedV,
  typename DerivedQ>
IGL_INLINE void igl::dual_contouring(
  const std::function<typename DerivedV::Scalar(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f,
  const std::function<Eigen::Matrix<typename DerivedV::Scalar,1,3>(const Eigen::Matrix<typename DerivedV::Scalar,1,3> &)> & f_grad,
  const Eigen::Matrix<typename DerivedV::Scalar,1,3> & step,
  const Eigen::MatrixBase<DerivedGf> & Gf,
  const Eigen::MatrixBase<DerivedGV> & GV,
  const Eigen::MatrixBase<DerivedGI> & GI,
  const bool constrained,
  const bool triangles,
  const bool root_finding,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedQ> & Q)
{
  if(GI.rows() == 0){ return;}
  assert(GI.cols() == 2);
  typedef typename DerivedV::Scalar Scalar;
  DualContouring<Scalar> DC(f,f_grad,constrained,triangles,root_finding);
  DC.sparse(step,Gf,GV,GI);
  V = DC.V;
  Q = DC.Q.template cast<typename DerivedQ::Scalar>();
}

#ifdef IGL_STATIC_LIBRARY


template void igl::dual_contouring<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 3, 1, -1, 3> > const&, int, int, int, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, int, int, int, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, int, int, int, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 2, 0, -1, 2>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::Matrix<Eigen::Matrix<double, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 2, 0, -1, 2> > const&, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<float, -1, 1, 0, -1, 1>, Eigen::Matrix<float, -1, 3, 1, -1, 3>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, 3, 1, -1, 3> > const&, int, int, int, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<float, -1, 1, 0, -1, 1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, int, int, int, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, int, int, int, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<float, -1, 1, 0, -1, 1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::dual_contouring<Eigen::Matrix<float, -1, 1, 0, -1, 1>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 2, 0, -1, 2>, Eigen::Matrix<float, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::function<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, std::function<Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> (Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&)> const&, Eigen::Matrix<Eigen::Matrix<float, -1, -1, 0, -1, -1>::Scalar, 1, 3, 1, 1, 3> const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, 1, 0, -1, 1> > const&, Eigen::MatrixBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, 2, 0, -1, 2> > const&, bool, bool, bool, Eigen::PlainObjectBase<Eigen::Matrix<float, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#endif
