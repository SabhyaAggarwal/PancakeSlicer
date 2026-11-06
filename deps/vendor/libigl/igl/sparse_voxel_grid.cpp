#include "sparse_voxel_grid.h"

#include <unordered_map>
#include <array>
#include <vector>
#include <cstdint>


template <typename DerivedP0, typename Func, typename DerivedS, typename DerivedV, typename DerivedI>
IGL_INLINE void igl::sparse_voxel_grid(const Eigen::MatrixBase<DerivedP0>& p0,
                                       const Func& scalarFunc,
                                       const double eps,
                                       const int expected_number_of_cubes,
                                       Eigen::PlainObjectBase<DerivedS>& CS,
                                       Eigen::PlainObjectBase<DerivedV>& CV,
                                       Eigen::PlainObjectBase<DerivedI>& CI)
{
  typedef typename DerivedV::Scalar ScalarV;
  typedef typename DerivedS::Scalar ScalarS;
  typedef typename DerivedI::Scalar ScalarI;
  typedef Eigen::Matrix<ScalarV, 1, 3> VertexRowVector;
  typedef Eigen::Matrix<ScalarI, 1, 8> IndexRowVector;


  struct IndexRowVectorHash {
    std::size_t operator()(const Eigen::RowVector3i& key) const {
      std::size_t seed = 0;
      std::hash<int> hasher;
      for (int i = 0; i < 3; i++) {
        seed ^= hasher(key[i]) + 0x9e3779b9 + (seed<<6) + (seed>>2);
      }
      return seed;
    }
  };

  auto sgn = [](ScalarS val) -> int {
    return (ScalarS(0) < val) - (val < ScalarS(0));
  };

  ScalarV half_eps = 0.5 * eps;

  std::vector<IndexRowVector> CI_vector;
  std::vector<VertexRowVector> CV_vector;
  std::vector<ScalarS> CS_vector;
  CI_vector.reserve(expected_number_of_cubes);
  CV_vector.reserve(8 * expected_number_of_cubes);
  CS_vector.reserve(8 * expected_number_of_cubes);


  std::unordered_map<Eigen::RowVector3i, int, IndexRowVectorHash> visited;
  visited.reserve(6 * expected_number_of_cubes);
  visited.max_load_factor(0.5);


  std::vector<Eigen::RowVector3i> queue;
  queue.reserve(expected_number_of_cubes * 8);
  queue.push_back(Eigen::RowVector3i(0, 0, 0));

  while (queue.size() > 0)
  {
    Eigen::RowVector3i pi = queue.back();
    queue.pop_back();
    if(visited.count(pi)){ continue; }

    VertexRowVector ctr = p0 + eps*pi.cast<ScalarV>();


    const Eigen::RowVector3i bx(1, 0, 0), by(0, 1, 0), bz(0, 0, -1);
    const std::array<Eigen::RowVector3i, 26> neighbors = {
      bx, -bx, by, -by, bz, -bz,
      by-bz, -by+bz,
      bx+by, -bx-by,
      by+bz, -by-bz,
      by-bx, -by+bx,
      bx-bz, -bx+bz,
      bx+bz, -bx-bz,
      -bx+by+bz, bx-by-bz,
      bx+by+bz, -bx-by-bz,
      bx+by-bz, -bx-by+bz,
      -bx+by-bz, bx-by+bz,
  };













    std::array<VertexRowVector, 8> cubeCorners = {
      ctr+half_eps*(bx+by+bz).cast<ScalarV>(), ctr+half_eps*(bx+by-bz).cast<ScalarV>(), ctr+half_eps*(-bx+by-bz).cast<ScalarV>(), ctr+half_eps*(-bx+by+bz).cast<ScalarV>(),
      ctr+half_eps*(bx-by+bz).cast<ScalarV>(), ctr+half_eps*(bx-by-bz).cast<ScalarV>(), ctr+half_eps*(-bx-by-bz).cast<ScalarV>(), ctr+half_eps*(-bx-by+bz).cast<ScalarV>()
    };
    std::array<ScalarS, 8> cubeScalars;
    for (int i = 0; i < 8; i++) { cubeScalars[i] = scalarFunc(cubeCorners[i]); }


    bool validCube = false;
    int sign = sgn(cubeScalars[0]);
    for (int i = 1; i < 8; i++) {
      if (sign != sgn(cubeScalars[i])) {
        validCube = true;
        break;
      }
    }
    if (!validCube) {
      continue;
    }


    IndexRowVector cube;
    std::uint8_t vertexAlreadyAdded = 0;
    constexpr std::array<std::uint8_t, 26> zv = {
      (1 << 0) | (1 << 1) | (1 << 4) | (1 << 5),
      (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7),
      (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3),
      (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7),
      (1 << 0) | (1 << 3) | (1 << 4) | (1 << 7),
      (1 << 1) | (1 << 2) | (1 << 5) | (1 << 6),
      (1 << 1) | (1 << 2),
      (1 << 4) | (1 << 7),
      (1 << 0) | (1 << 1),
      (1 << 6) | (1 << 7),
      (1 << 0) | (1 << 3),
      (1 << 5) | (1 << 6),
      (1 << 2) | (1 << 3),
      (1 << 4) | (1 << 5),
      (1 << 1) | (1 << 5),
      (1 << 3) | (1 << 7),
      (1 << 0) | (1 << 4),
      (1 << 2) | (1 << 6),
      (1 << 3), (1 << 5),
      (1 << 0), (1 << 6),
      (1 << 1), (1 << 7),
      (1 << 2), (1 << 4),
    };
    constexpr std::array<std::array<int, 4>, 26> zvv {{
      {{0, 1, 4, 5}}, {{3, 2, 7, 6}}, {{0, 1, 2, 3}},
      {{4, 5, 6, 7}}, {{0, 3, 4, 7}}, {{1, 2, 5, 6}},
      {{-1,-1,1,2}}, {{-1,-1,4,7}}, {{-1,-1,0,1}},{{-1,-1,7,6}},
      {{-1,-1,0,3}}, {{-1,-1,5,6}}, {{-1,-1,2,3}}, {{-1,-1,5,4}},
      {{-1,-1,1,5}}, {{-1,-1,3,7}}, {{-1,-1,0,4}}, {{-1,-1,2,6}},
      {{-1,-1,-1,3}}, {{-1,-1,-1,5}}, {{-1,-1,-1,0}}, {{-1,-1,-1,6}},
      {{-1,-1,-1,1}}, {{-1,-1,-1,7}}, {{-1,-1,-1,2}}, {{-1,-1,-1,4}} }};

    for (int n = 0; n < 26; n++) {
      Eigen::RowVector3i nkey = pi + neighbors[n];
      auto nbr = visited.find(nkey);
      if (nbr != visited.end()) {
        vertexAlreadyAdded |= zv[n];
        for (int i = 0; i < 4; i++)
        {
          if (zvv[n][i]!=-1)
          {
            cube[zvv[n][i]] = CI_vector[nbr->second][zvv[n % 2 == 0 ? n + 1 : n - 1][i]];
          }
        }
      } else {
        queue.push_back(nkey);
      }
    }

    for (int i = 0; i < 8; i++) {
      if (0 == ((1 << i) & vertexAlreadyAdded)) {
        cube[i] = CS_vector.size();
        CV_vector.push_back(cubeCorners[i]);
        CS_vector.push_back(cubeScalars[i]);
      }
    }

    visited[pi] = CI_vector.size();
    CI_vector.push_back(cube);
  }

  CV.conservativeResize(CV_vector.size(), 3);
  CS.conservativeResize(CS_vector.size(), 1);
  CI.conservativeResize(CI_vector.size(), 8);

  for (int i = 0; i < CV_vector.size(); i++) {
    CV.row(i) = CV_vector[i];
  }
  for (int i = 0; i < CS_vector.size(); i++) {
    CS(i) = CS_vector[i];
  }
  for (int i = 0; i < CI_vector.size(); i++) {
    CI.row(i) = CI_vector[i];
  }
}


#ifdef IGL_STATIC_LIBRARY


template void igl::sparse_voxel_grid<Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 8, 0, -1, 8> >(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)> const&, double, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 8, 0, -1, 8> >&);
template void igl::sparse_voxel_grid<class Eigen::Matrix<double, -1, -1, 0, -1, -1>, class std::function<double(class Eigen::Matrix<double, -1, -1, 0, -1, -1> const &)>, class Eigen::Matrix<double, -1, 1, 0, -1, 1>, class Eigen::Matrix<double, -1, -1, 0, -1, -1>, class Eigen::Matrix<int, -1, -1, 0, -1, -1> >(class Eigen::MatrixBase<class Eigen::Matrix<double, -1, -1, 0, -1, -1> > const &, class std::function<double(class Eigen::Matrix<double, -1, -1, 0, -1, -1> const &)> const &, double, int, class Eigen::PlainObjectBase<class Eigen::Matrix<double, -1, 1, 0, -1, 1> > &, class Eigen::PlainObjectBase<class Eigen::Matrix<double, -1, -1, 0, -1, -1> > &, class Eigen::PlainObjectBase<class Eigen::Matrix<int, -1, -1, 0, -1, -1> > &);
template void igl::sparse_voxel_grid<Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)>, Eigen::Matrix<double, -1, 1, 0, -1, 1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)> const&, double, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
template void igl::sparse_voxel_grid<Eigen::Matrix<double, 1, 3, 1, 1, 3>, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, std::function<double (Eigen::Matrix<double, 1, 3, 1, 1, 3> const&)> const&, double, int, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#endif
