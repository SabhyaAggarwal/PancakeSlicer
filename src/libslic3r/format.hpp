#ifndef slic3r_format_hpp_
#define slic3r_format_hpp_

#include <boost/format.hpp>
#include <string>

namespace Slic3r {

namespace internal {
	namespace format {
		template<typename T>
		inline T&& cook(T&& arg) {
		  	return std::forward<T>(arg);
		}

		inline std::string format_recursive(boost::format& message) {
		  	return message.str();
		}

		template<typename TValue, typename... TArgs>
		std::string format_recursive(boost::format& message, TValue&& arg, TArgs&&... args) {
		  	message % cook(std::forward<TValue>(arg));
		  	return format_recursive(message, std::forward<TArgs>(args)...);
		}
	}
};

template<typename... TArgs>
inline std::string format(const char* fmt, TArgs&&... args) {
	boost::format message(fmt);
	return internal::format::format_recursive(message, std::forward<TArgs>(args)...);
}

template<typename... TArgs>
inline std::string format(const std::string& fmt, TArgs&&... args) {
	boost::format message(fmt);
	return internal::format::format_recursive(message, std::forward<TArgs>(args)...);
}

}

#endif
