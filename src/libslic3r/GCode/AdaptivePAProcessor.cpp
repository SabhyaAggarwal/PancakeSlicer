#include "../GCode.hpp"
#include "libslic3r/GCode/AdaptivePAInterpolator.hpp"
#include "libslic3r/libslic3r.h"
#include "AdaptivePAProcessor.hpp"
#include <memory>
#include <cstddef>
#include <regex>
#include <iosfwd>
#include <algorithm>
#include <exception>
#include <sstream>
#include <iostream>
#include <cmath>
#include <cctype>
#include <string>
#include <utility>
#include "libslic3r/Config.hpp"
#include "libslic3r/GCodeWriter.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

AdaptivePAProcessor::AdaptivePAProcessor(GCode &gcodegen)
    : m_gcodegen(gcodegen),
      m_config(gcodegen.config()),
      m_last_predicted_pa(0.0),
      m_max_next_feedrate(0.0),
      m_next_feedrate(0.0),
      m_current_feedrate(0.0),
      m_last_config_index(-1),
      m_pa_change_pattern(R"(; PA_CHANGE:T(\d+) MM3MM:([0-9]*\.[0-9]+) ACCEL:(\d+) BR:(\d+) RC:(\d+) OV:(\d+))"),
      m_g1_f_pattern(R"(G1 F([0-9]+))")
{
    const size_t indices = std::max(m_config.adaptive_pressure_advance.size(), m_config.enable_pressure_advance.size());
    for (size_t i = 0; i < indices && !m_enabled; ++i)
        m_enabled = m_config.adaptive_pressure_advance.get_at(i) && m_config.enable_pressure_advance.get_at(i);
}

AdaptivePAInterpolator* AdaptivePAProcessor::getInterpolator(unsigned int config_index) {
    auto [it, inserted] = m_AdaptivePAInterpolators.try_emplace(config_index);
    if (inserted && m_config.adaptive_pressure_advance.get_at(config_index) && m_config.enable_pressure_advance.get_at(config_index)) {
        it->second = std::make_unique<AdaptivePAInterpolator>();
        it->second->parseAndSetData(m_config.adaptive_pressure_advance_model.get_at(config_index));
    }
    return it->second.get();
}

std::string AdaptivePAProcessor::process_layer(std::string &&gcode) {
    if (!m_enabled && gcode.find("; PA_CHANGE") == std::string::npos) {
        if (!gcode.empty() && gcode.back() != '\n')
            gcode += '\n';
        return std::move(gcode);
    }
    std::istringstream stream(gcode);
    std::string line;
    std::ostringstream output;
    double mm3mm_value = 0.0;
    unsigned int accel_value = 0;
    std::string pa_change_line;
    bool wipe_command = false;

    while (std::getline(stream, line)) {
        
        if (line.find("WIPE_START") != std::string::npos) {
            wipe_command = true;
        }
                
        if ( (line.find("G1 F") == 0) && (!wipe_command) ) {
            std::size_t pos = line.find('F');
            if (pos != std::string::npos){
                m_current_feedrate = std::stod(line.substr(pos + 1)) / 60.0;
            }
        }
        
        if (line.find("WIPE_END") != std::string::npos) {
            wipe_command = false;
        }
        
        m_next_feedrate = 0;
        
        if (line.find("; PA_CHANGE") == 0) {
            if (std::regex_search(line, m_match, m_pa_change_pattern)) {
                int config_index = std::stoi(m_match[1].str());
                mm3mm_value = std::stod(m_match[2].str());
                accel_value = std::stod(m_match[3].str());
                int isBridge = std::stoi(m_match[4].str());
                int roleChange = std::stoi(m_match[5].str());
                int isOverhang = std::stoi(m_match[6].str());
                
                bool config_index_changed = (config_index != m_last_config_index);
                m_last_config_index = config_index;
                
                pa_change_line = line;
                
                std::streampos current_pos = stream.tellg();
                std::string next_line;
                double temp_feed_rate = 0;
                bool extrude_move_found = false;
                int line_counter = 0;
                
                while (std::getline(stream, next_line)) {
                    line_counter++;
                    if ((!extrude_move_found) && next_line.find("G1 ") == 0 &&
                        next_line.find('X') != std::string::npos &&
                        next_line.find('Y') != std::string::npos &&
                        next_line.find('E') != std::string::npos) {
                        extrude_move_found = true;
                        continue;
                    }
                    
                    if (next_line.find("G1 ") == 0 &&
                        next_line.find('X') != std::string::npos &&
                        next_line.find('Y') != std::string::npos &&
                        next_line.find('E') == std::string::npos &&
                        extrude_move_found) {
                        break;
                    }
                    
                    if (next_line.find("WIPE") != std::string::npos) {
                        break;
                    }
                    
                    if (next_line.find("; PA_CHANGE") == 0) {
                        std::size_t rc_pos = next_line.rfind("RC:");
                        if (rc_pos != std::string::npos) {
                            int rc_value = std::stoi(next_line.substr(rc_pos + 3));
                            if (rc_value == 1) {
                                break;
                            }
                        }
                    }
                    
                    if (next_line.find("G1 F") == 0) {
                        std::size_t pos = next_line.find('F');
                        if (pos != std::string::npos) {
                            double feedrate = std::stod(next_line.substr(pos + 1)) / 60.0;
                            if(line_counter==1){
                                m_current_feedrate = feedrate;
                            }
                            if (temp_feed_rate < feedrate) {
                                temp_feed_rate = feedrate;
                            }
                            if(m_next_feedrate < EPSILON){
                                m_next_feedrate = feedrate;
                            }
                        }
                        continue;
                    }
                }
                
                if (temp_feed_rate > 0) {
                    m_max_next_feedrate = temp_feed_rate;
                } else
                    m_max_next_feedrate = m_current_feedrate;
                
                stream.clear();
                stream.seekg(current_pos);
                
                AdaptivePAInterpolator* interpolator = getInterpolator(m_last_config_index);
                
                double predicted_pa = 0;
                double adaptive_PA_speed = 0;
            
                if(!interpolator){
                    predicted_pa = m_config.enable_pressure_advance.get_at(m_last_config_index) ? m_config.pressure_advance.get_at(m_last_config_index) : 0;
                    if(m_config.gcode_comments) output << "; APA: Tool doesnt have APA enabled\n";
                } else if (!interpolator->isInitialised() || (!m_config.adaptive_pressure_advance.get_at(m_last_config_index)) )
                {
                    predicted_pa = m_config.enable_pressure_advance.get_at(m_last_config_index) ? m_config.pressure_advance.get_at(m_last_config_index) : 0;
                    if(m_config.gcode_comments) output << "; APA: Interpolator setup failed, using default pressure advance\n";
                } else {
                    if(isOverhang > 0){
                        adaptive_PA_speed = (m_current_feedrate == 0 || m_next_feedrate == 0) ?
                                                std::max(m_current_feedrate, m_next_feedrate) :
                                                std::min(m_current_feedrate, m_next_feedrate);
                    }else{
                        adaptive_PA_speed = std::max(m_max_next_feedrate,m_current_feedrate);
                    }
                    
                    predicted_pa = (*interpolator)(mm3mm_value * adaptive_PA_speed, accel_value);
                    
                    if(isBridge && m_config.adaptive_pressure_advance_bridges.get_at(m_last_config_index) > EPSILON)
                        predicted_pa = m_config.adaptive_pressure_advance_bridges.get_at(m_last_config_index);
                    
                    if (predicted_pa < 0) {
                        predicted_pa = m_config.enable_pressure_advance.get_at(m_last_config_index) ? m_config.pressure_advance.get_at(m_last_config_index) : 0;
                        if(m_config.gcode_comments) output << "; APA: Interpolation failed, using fallback pressure advance value\n";
                    }
                }
                if(m_config.gcode_comments) {
                    output << pa_change_line << '\n';
                    if(isBridge && m_config.adaptive_pressure_advance_bridges.get_at(m_last_config_index) > EPSILON)
                        output << "; APA Model Override (bridge)\n";
                    output << "; APA Current Speed: " << std::to_string(m_current_feedrate) << "\n";
                    output << "; APA Next Speed: " << std::to_string(m_next_feedrate) << "\n";
                    output << "; APA Max Next Speed: " << std::to_string(m_max_next_feedrate) << "\n";
                    output << "; APA Speed Used: " << std::to_string(adaptive_PA_speed) << "\n";
                    output << "; APA Flow rate: " << std::to_string(mm3mm_value * m_max_next_feedrate) << "\n";
                    output << "; APA Prev PA: " << std::to_string(m_last_predicted_pa) << " New PA: " << std::to_string(predicted_pa) << "\n"; 
                }
                if (config_index_changed || std::fabs(predicted_pa - m_last_predicted_pa) > EPSILON) {
                    output << m_gcodegen.writer().set_pressure_advance(predicted_pa);
                    m_last_predicted_pa = predicted_pa;
                }
            }
        }else {
            output << line << '\n';
        }
    }

    return output.str();
}

std::string AdaptivePAProcessor::validate_adaptive_pa_model(const std::string& model_str)
{
    if (model_str.empty())
        return {};
    
    std::istringstream model_stream(model_str);
    std::string line;
    int line_number = 0;
    
    while (std::getline(model_stream, line)) {
        ++line_number;
        
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        
        const auto last = line.find_last_not_of(" \t\r\n");
        line = line.substr(first, last - first + 1);
        
        for (char c : line) {
            if (!std::isdigit(static_cast<unsigned char>(c)) && c != ',' && c != '.') {
                return "Line " + std::to_string(line_number) +
                       ": only numbers, commas and dots are allowed";
            }
        }

        int comma_count = 0;
        for (char c : line) {
            if (c == ',') comma_count++;
        }
        
        if (comma_count != 2) {
            return "Line " + std::to_string(line_number) + 
                   ": must contain exactly 3 comma-separated values (PA, flow, acceleration)";
        }
        
        try {
            std::istringstream line_stream(line);
            std::string value;
            
            if (!std::getline(line_stream, value, ','))
                return "Line " + std::to_string(line_number) + ": missing PA value";
            double pa = std::stod(value);
            
            if (!std::getline(line_stream, value, ','))
                return "Line " + std::to_string(line_number) + ": missing flow value";
            double flow = std::stod(value);
            
            if (!std::getline(line_stream, value, ','))
                return "Line " + std::to_string(line_number) + ": missing acceleration value";
            double accel = std::stod(value);
            
            if (pa >= 2.0) {
                return "Line " + std::to_string(line_number) + ": PA value must be less than 2";
            }
            if (flow <= pa) {
                return "Line " + std::to_string(line_number) + ": flow value must be greater than PA value";
            }
            if (accel <= flow) {
                return "Line " + std::to_string(line_number) + ": acceleration value must be greater than flow value";
            }
        } catch (const std::exception&) {
            return "Line " + std::to_string(line_number) + ": invalid numeric value";
        }
    }
    
    return {};
}

}
