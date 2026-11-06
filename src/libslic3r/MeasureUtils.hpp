#ifndef Slic3r_MeasureUtils_hpp_
#define Slic3r_MeasureUtils_hpp_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "Point.hpp"

namespace Slic3r {
namespace Measure {





class Polynomial1
{
public:
    Polynomial1(std::initializer_list<double> values)
    {



        m_coefficient.resize(values.size());
        std::copy(values.begin(), values.end(), m_coefficient.begin());
        EliminateLeadingZeros();
    }








    explicit Polynomial1(uint32_t degree)
        : m_coefficient(static_cast<size_t>(degree) + 1, 0.0)
    {}










    void EliminateLeadingZeros()
    {
        const size_t size = m_coefficient.size();
        if (size > 1) {
            const double zero = 0.0;
            int32_t leading;
            for (leading = static_cast<int32_t>(size) - 1; leading > 0; --leading) {
                if (m_coefficient[leading] != zero)
                    break;
            }

            m_coefficient.resize(++leading);
        }
    }


    void SetCoefficients(double value)
    {
        std::fill(m_coefficient.begin(), m_coefficient.end(), value);
    }

    inline uint32_t GetDegree() const
    {

        return static_cast<uint32_t>(m_coefficient.size() - 1);
    }

    inline const double& operator[](uint32_t i) const { return m_coefficient[i]; }
    inline double& operator[](uint32_t i) { return m_coefficient[i]; }



    double operator()(double t) const
    {
        int32_t i = static_cast<int32_t>(m_coefficient.size());
        double result = m_coefficient[--i];
        for (--i; i >= 0; --i) {
            result *= t;
            result += m_coefficient[i];
        }
        return result;
    }

protected:

    std::vector<double> m_coefficient;
};

inline Polynomial1 operator * (const Polynomial1& p0, const Polynomial1& p1)
{
    const uint32_t p0Degree = p0.GetDegree();
    const uint32_t p1Degree = p1.GetDegree();
    Polynomial1 result(p0Degree + p1Degree);
    result.SetCoefficients(0.0);
    for (uint32_t i0 = 0; i0 <= p0Degree; ++i0) {
        for (uint32_t i1 = 0; i1 <= p1Degree; ++i1) {
            result[i0 + i1] += p0[i0] * p1[i1];
        }
    }
    return result;
}

inline Polynomial1 operator + (const Polynomial1& p0, const Polynomial1& p1)
{
    const uint32_t p0Degree = p0.GetDegree();
    const uint32_t p1Degree = p1.GetDegree();
    uint32_t i;
    if (p0Degree >= p1Degree) {
        Polynomial1 result(p0Degree);
        for (i = 0; i <= p1Degree; ++i) {
            result[i] = p0[i] + p1[i];
        }
        for ( ; i <= p0Degree; ++i) {
            result[i] = p0[i];
        }
        result.EliminateLeadingZeros();
        return result;
    }
    else {
        Polynomial1 result(p1Degree);
        for (i = 0; i <= p0Degree; ++i) {
            result[i] = p0[i] + p1[i];
        }
        for ( ; i <= p1Degree; ++i) {
            result[i] = p1[i];
        }
        result.EliminateLeadingZeros();
        return result;
    }
}

inline Polynomial1 operator - (const Polynomial1& p0, const Polynomial1& p1)
{
    const uint32_t p0Degree = p0.GetDegree();
    const uint32_t p1Degree = p1.GetDegree();
    uint32_t i;
    if (p0Degree >= p1Degree) {
        Polynomial1 result(p0Degree);
        for (i = 0; i <= p1Degree; ++i) {
            result[i] = p0[i] - p1[i];
        }
        for ( ; i <= p0Degree; ++i) {
            result[i] = p0[i];
        }
        result.EliminateLeadingZeros();
        return result;
    }
    else {
        Polynomial1 result(p1Degree);
        for (i = 0; i <= p0Degree; ++i) {
            result[i] = p0[i] - p1[i];
        }
        for ( ; i <= p1Degree; ++i) {
            result[i] = -p1[i];
        }
        result.EliminateLeadingZeros();
        return result;
    }
}

inline Polynomial1 operator * (double scalar, const Polynomial1& p)
{
    const uint32_t degree = p.GetDegree();
    Polynomial1 result(degree);
    for (uint32_t i = 0; i <= degree; ++i) {
        result[i] = scalar * p[i];
    }
    return result;
}





class RootsPolynomial
{
public:





    static int32_t Find(int32_t degree, const double* c, uint32_t maxIterations, double* roots)
    {
        if (degree >= 0 && c != nullptr) {
            const double zero = 0.0;
            while (degree >= 0 && c[degree] == zero) {
                --degree;
            }

            if (degree > 0) {

                const double one = 1.0;
                const double invLeading = one / c[degree];
                double maxValue = zero;
                for (int32_t i = 0; i < degree; ++i) {
                    const double value = std::fabs(c[i] * invLeading);
                    if (value > maxValue)
                        maxValue = value;
                }
                const double bound = one + maxValue;

                return FindRecursive(degree, c, -bound, bound, maxIterations, roots);
            }
            else if (degree == 0)

                return 0;
            else {

                roots[0] = zero;
                return 1;
            }
        }
        else

            return 0;
    }



    static bool Find(int32_t degree, const double* c, double tmin, double tmax, uint32_t maxIterations, double& root)
    {
        const double zero = 0.0;
        double pmin = Evaluate(degree, c, tmin);
        if (pmin == zero) {
            root = tmin;
            return true;
        }
        double pmax = Evaluate(degree, c, tmax);
        if (pmax == zero) {
            root = tmax;
            return true;
        }

        if (pmin * pmax > zero)

            return false;

        if (tmin >= tmax)

            return false;

        for (uint32_t i = 1; i <= maxIterations; ++i) {
            root = 0.5 * (tmin + tmax);



            if (root == tmin || root == tmax)
                break;

            const double p = Evaluate(degree, c, root);
            const double product = p * pmin;
            if (product < zero) {
                tmax = root;
                pmax = p;
            }
            else if (product > zero) {
                tmin = root;
                pmin = p;
            }
            else
                break;
        }

        return true;
    }


    static int32_t FindRecursive(int32_t degree, double const* c, double tmin, double tmax, uint32_t maxIterations, double* roots)
    {

        const double zero = 0.0;
        double root = zero;
        if (degree == 1) {
            int32_t numRoots;
            if (c[1] != zero) {
                root = -c[0] / c[1];
                numRoots = 1;
            }
            else if (c[0] == zero) {
                root = zero;
                numRoots = 1;
            }
            else
                numRoots = 0;

            if (numRoots > 0 && tmin <= root && root <= tmax) {
                roots[0] = root;
                return 1;
            }
            return 0;
        }







        const int32_t derivDegree = degree - 1;
        std::vector<double> derivCoeff(static_cast<size_t>(derivDegree) + 1);
        std::vector<double> derivRoots(derivDegree);
        for (int32_t i = 0, ip1 = 1; i <= derivDegree; ++i, ++ip1) {
            derivCoeff[i] = c[ip1] * (double)(ip1) / (double)degree;
        }
        const int32_t numDerivRoots = FindRecursive(degree - 1, &derivCoeff[0], tmin, tmax, maxIterations, &derivRoots[0]);

        int32_t numRoots = 0;
        if (numDerivRoots > 0) {

            if (Find(degree, c, tmin, derivRoots[0], maxIterations, root))
                roots[numRoots++] = root;


            for (int32_t i = 0, ip1 = 1; i <= numDerivRoots - 2; ++i, ++ip1) {
                if (Find(degree, c, derivRoots[i], derivRoots[ip1], maxIterations, root))
                    roots[numRoots++] = root;
            }


            if (Find(degree, c, derivRoots[static_cast<size_t>(numDerivRoots) - 1], tmax, maxIterations, root))
                roots[numRoots++] = root;
        }
        else {

            if (Find(degree, c, tmin, tmax, maxIterations, root))
                roots[numRoots++] = root;
        }
        return numRoots;
    }

    static double Evaluate(int32_t degree, const double* c, double t)
    {
        int32_t i = degree;
        double result = c[i];
        while (--i >= 0) {
            result = t * result + c[i];
        }
        return result;
    }
};








inline Vec3d get_orthogonal(const Vec3d& v, bool unitLength)
{
    double cmax = std::fabs(v[0]);
    int32_t imax = 0;
    for (int32_t i = 1; i < 3; ++i) {
        double c = std::fabs(v[i]);
        if (c > cmax) {
            cmax = c;
            imax = i;
        }
    }

    Vec3d result = Vec3d::Zero();
    int32_t inext = imax + 1;
    if (inext == 3)
        inext = 0;

    result[imax] = v[inext];
    result[inext] = -v[imax];
    if (unitLength) {
        const double sqrDistance = result[imax] * result[imax] + result[inext] * result[inext];
        const double invLength = 1.0 / std::sqrt(sqrDistance);
        result[imax] *= invLength;
        result[inext] *= invLength;
    }
    return result;
}

}
}

#endif
