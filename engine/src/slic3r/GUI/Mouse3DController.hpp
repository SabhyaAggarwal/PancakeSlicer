#ifndef slic3r_Mouse3DController_hpp_
#define slic3r_Mouse3DController_hpp_

#include <cstddef>
#include <limits>
#include <deque>
#include <map>
#include <string>
#include <array>
#define ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT 0

#include "libslic3r/Point.hpp"

#include "hidapi.h"

#include <queue>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <chrono>
#include <condition_variable>


namespace Slic3r {

class AppConfig;

namespace GUI {

struct Camera;
class GLCanvas3D;

class Mouse3DController
{
	struct Params
	{
        static constexpr double MinTranslationScale = 0.1;
        static constexpr double MaxTranslationScale = 30.;
		static constexpr double DefaultTranslationScale = 2.5;
        static constexpr double MaxTranslationDeadzone = 0.2;
        static constexpr double DefaultTranslationDeadzone = 0.0;
        static constexpr float  DefaultRotationScale = 1.0f;
        static constexpr float  MaxRotationDeadzone = 0.2f;
        static constexpr float  DefaultRotationDeadzone = 0.0f;
        static constexpr double DefaultZoomScale = 0.1;

        template <typename Number>
        struct CustomParameters
        {
            Number scale;
            Number deadzone;
        };

        CustomParameters<double> translation { DefaultTranslationScale, DefaultTranslationDeadzone };
        CustomParameters<float>  rotation { DefaultRotationScale, DefaultRotationDeadzone };
        CustomParameters<double> zoom { DefaultZoomScale, 0.0 };
        bool 					 buttons_enabled { false };
        size_t 					 input_queue_max_size { 15 };
        bool 					 swap_yz{ false };
        bool                                     invert_x{ false };
        bool                                     invert_y{ false };
        bool                                     invert_z{ false };
        bool                                     invert_yaw{ false };
        bool                                     invert_pitch{ false };
        bool                                     invert_roll{ false };
    };

    class State
    {
    public:
        struct QueueItem {
            static QueueItem translation(const Vec3d &translation) { QueueItem out; out.vector = translation; out.type_or_buttons = TranslationType; return out; }
            static QueueItem rotation(const Vec3d &rotation) { QueueItem out; out.vector = rotation; out.type_or_buttons = RotationType; return out; }
            static QueueItem buttons(unsigned int buttons) { QueueItem out; out.type_or_buttons = buttons; return out; }

            bool 			 is_translation() const { return this->type_or_buttons == TranslationType; }
            bool 			 is_rotation() const { return this->type_or_buttons == RotationType; }
            bool 			 is_buttons() const { return ! this->is_translation() && ! this->is_rotation(); }

            Vec3d        	 vector;
            unsigned int 	 type_or_buttons;

            static constexpr unsigned int TranslationType = std::numeric_limits<unsigned int>::max();
            static constexpr unsigned int RotationType    = TranslationType - 1;
        };

    private:
        std::deque<QueueItem> m_input_queue;
        mutable std::mutex	  m_input_queue_mutex;

#ifdef WIN32
        unsigned int 		  m_mouse_wheel_counter { 0 };
#endif

    public:
        void append_translation(const Vec3d& translation, size_t input_queue_max_size);
        void append_rotation(const Vec3f& rotation, size_t input_queue_max_size);
        void append_button(unsigned int id, size_t input_queue_max_size);

#ifdef WIN32
        bool process_mouse_wheel();
#endif

#if ENABLE_3DCONNEXION_DEVICES_DEBUG_OUTPUT
        Vec3d               get_first_vector_of_type(unsigned int type) const {
            std::scoped_lock<std::mutex> lock(m_input_queue_mutex);
            auto it = std::find_if(m_input_queue.begin(), m_input_queue.end(), [type](const QueueItem& item) { return item.type_or_buttons == type; });
            return (it == m_input_queue.end()) ? Vec3d::Zero() : it->vector;
        }
        size_t              input_queue_size_current() const { 
        	std::scoped_lock<std::mutex> lock(m_input_queue_mutex); 
        	return m_input_queue.size(); 
        }
        std::atomic<size_t> input_queue_max_size_achieved;
#endif
        bool apply(const Params &params, Camera& camera);
    };

    Params 				m_params;
    Params 				m_params_ui;
    bool 	            m_params_ui_changed { false };
    mutable std::mutex	m_params_ui_mutex;

    std::map<std::string, Params> m_params_by_device;

    mutable State 		m_state;
    std::atomic<bool> 	m_connected { false };
    std::string 		m_device_str;

#if ! __APPLE__
    std::thread 		m_thread;
    hid_device* 		m_device { nullptr };
    bool				m_stop { false };
#ifdef _WIN32
    std::atomic<bool>	m_wakeup { false };
#endif
	std::mutex 			m_stop_condition_mutex;
   	std::condition_variable m_stop_condition;
#endif

    mutable bool 		m_show_settings_dialog { false };
    mutable bool 		m_settings_dialog_closed_by_user { false };

public:
	void load_config(const AppConfig &appconfig);
	void save_config(AppConfig &appconfig) const;
    void init();
    void shutdown();

    bool connected() const { return m_connected; }

#if __APPLE__
    void connected(std::string device_name);
    void disconnected();
    typedef std::array<double, 6> DataPacketAxis;
    bool handle_input(const DataPacketAxis& packet);
#endif

#ifdef _WIN32
	bool handle_raw_input_win32(const unsigned char *data, const int packet_lenght);

    void device_attached(const std::string &device);
    void device_detached(const std::string& device);

    bool process_mouse_wheel() { return m_state.process_mouse_wheel(); }
#endif
    bool apply(Camera& camera);

    bool is_settings_dialog_shown() const { return m_show_settings_dialog; }
    void show_settings_dialog(bool show) { m_show_settings_dialog = show && this->connected(); }
    void render_settings_dialog(GLCanvas3D& canvas) const;

#if ! __APPLE__
private:
    bool connect_device();
    void disconnect_device();

    void run();
    void collect_input();

    typedef std::array<unsigned char, 13> DataPacketRaw;

    static bool handle_input(const DataPacketRaw& packet, const int packet_length, const Params &params, State &state_in_out);
    static bool handle_packet(const DataPacketRaw& packet, const int packet_length, const Params &params, State &state_in_out);
    static bool handle_packet_translation(const DataPacketRaw& packet, const Params &params, State &state_in_out);
    static bool handle_packet_rotation(const DataPacketRaw& packet, unsigned int first_byte, const Params &params, State &state_in_out);
    static bool handle_packet_button(const DataPacketRaw& packet, unsigned int packet_size, const Params &params, State &state_in_out);
#endif
};

}
}


#endif
