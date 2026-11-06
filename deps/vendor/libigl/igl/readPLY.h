#ifndef IGL_READPLY_H
#define IGL_READPLY_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <string>
#include <vector>
#include "tinyply.h"

namespace igl
{























  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedN,
    typename DerivedUV,
    typename DerivedVD,
    typename DerivedFD,
    typename DerivedED
    >
  bool readPLY(
    std::istream & ply_stream,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedF> & E,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedUV> & UV,
    Eigen::PlainObjectBase<DerivedVD> & VD,
    std::vector<std::string> & Vheader,
    Eigen::PlainObjectBase<DerivedFD> & FD,
    std::vector<std::string> & Fheader,
    Eigen::PlainObjectBase<DerivedED> & ED,
    std::vector<std::string> & Eheader,
    std::vector<std::string> & comments
    );


  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedN,
    typename DerivedUV,
    typename DerivedVD,
    typename DerivedFD,
    typename DerivedED
    >
  bool readPLY(
    const std::string& ply_file,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedUV> & UV,
    Eigen::PlainObjectBase<DerivedVD> & VD,
    std::vector<std::string> & VDheader,
    Eigen::PlainObjectBase<DerivedFD> & FD,
    std::vector<std::string> & FDheader,
    Eigen::PlainObjectBase<DerivedED> & ED,
    std::vector<std::string> & EDheader,
    std::vector<std::string> & comments
    );

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedN,
    typename DerivedUV,
    typename DerivedVD
    >
  bool readPLY(
    const std::string & filename,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedUV> & UV,
    Eigen::PlainObjectBase<DerivedVD> & VD,
    std::vector<std::string> & Vheader
    );

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedE,
    typename DerivedN,
    typename DerivedUV
    >
  bool readPLY(
    const std::string & filename,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E,
    Eigen::PlainObjectBase<DerivedN> & N,
    Eigen::PlainObjectBase<DerivedUV> & UV
    );

  template <
    typename DerivedV,
    typename DerivedF
    >
  bool readPLY(
    const std::string & filename,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F
    );

  template <
    typename DerivedV,
    typename DerivedF,
    typename DerivedE
    >
  bool readPLY(
    const std::string & filename,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedE> & E
    );


  template <
    typename DerivedV,
    typename DerivedF
    >
  IGL_INLINE bool readPLY(
    FILE *fp,
    Eigen::PlainObjectBase<DerivedV> & V,
    Eigen::PlainObjectBase<DerivedF> & F
    );
}

#ifndef IGL_STATIC_LIBRARY
#  include "readPLY.cpp"
#endif
#endif
