#ifndef IGL_LINSPACED_H
#define IGL_LINSPACED_H
#include <Eigen/Core>



























namespace igl
{







  template <typename Derived>

  inline Derived LinSpaced(
    typename Derived::Index size,
    const typename Derived::Scalar & low,
    const typename Derived::Scalar & high);
}



template <typename Derived>

inline Derived
igl::LinSpaced(
  typename Derived::Index size,
  const typename Derived::Scalar & low,
  const typename Derived::Scalar & high)
{
  if(size == 0)
  {

    return Derived::LinSpaced(0,0,1);
  }else if(high < low)
  {
    return low-Derived::LinSpaced(size,low-low,low-high).array();
  }else{
    return Derived::LinSpaced(size,low,high);
  }
}

#endif
