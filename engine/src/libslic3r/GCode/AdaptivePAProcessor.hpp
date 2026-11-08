#ifndef ADAPTIVEPAPROCESSOR_H
#define ADAPTIVEPAPROCESSOR_H

#include <string>
#include <sstream>
#include <regex>
#include <memory>
#include <map>
#include <unordered_map>
#include "AdaptivePAInterpolator.hpp"

namespace Slic3r {

class GCode;
class PrintConfig;

class AdaptivePAProcessor {
public:
    AdaptivePAProcessor(GCode &gcodegen);
    
    std::string process_layer(std::string &&gcode);
    
    void resetPreviousPA(double PA){ m_last_predicted_pa = PA; };
    
    static std::string validate_adaptive_pa_model(const std::string& model_str);
    
private:
    GCode &m_gcodegen;
    std::unordered_map<unsigned int, std::unique_ptr<AdaptivePAInterpolator>> m_AdaptivePAInterpolators;
    const PrintConfig &m_config;
    double m_last_predicted_pa;
    double m_max_next_feedrate;
    double m_next_feedrate;
    double m_current_feedrate;
    int m_last_config_index;
    bool m_enabled{false};

    std::regex m_pa_change_pattern;
    std::regex m_g1_f_pattern;
    std::smatch m_match;
    AdaptivePAInterpolator* getInterpolator(unsigned int config_index);
};

}

#endif
