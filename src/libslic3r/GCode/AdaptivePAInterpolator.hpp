#ifndef ADAPTIVEPAINTERPOLATOR_HPP
#define ADAPTIVEPAINTERPOLATOR_HPP

#include <vector>
#include <string>
#include <map>
#include "PchipInterpolatorHelper.hpp"

class AdaptivePAInterpolator {
public:
    AdaptivePAInterpolator() : m_isInitialised(false) {}

    int parseAndSetData(const std::string& data);

    double operator()(double flow_rate, double acceleration);
    
    bool isInitialised() const {
        return m_isInitialised;
    }

private:
    std::map<double, PchipInterpolatorHelper> flow_interpolators_;
    std::vector<double> accelerations_;
    bool m_isInitialised;
};

#endif
