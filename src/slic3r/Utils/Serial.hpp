#ifndef slic3r_GUI_Utils_Serial_hpp_
#define slic3r_GUI_Utils_Serial_hpp_

#include <utility>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/io_service.hpp>
#include <vector>
#include <string>
#include <boost/system/error_code.hpp>
#include <boost/asio.hpp>


namespace Slic3r {
namespace Utils {

struct SerialPortInfo {
	std::string port;
	unsigned    id_vendor = -1;
	unsigned    id_product = -1;
	std::string friendly_name;
	bool        is_printer = false;

	SerialPortInfo() {}
	SerialPortInfo(std::string port) : port(port), friendly_name(std::move(port)) {}

	bool id_match(unsigned id_vendor, unsigned id_product) const { return id_vendor == this->id_vendor && id_product == this->id_product; }
};

inline bool operator==(const SerialPortInfo &sp1, const SerialPortInfo &sp2)
{
	return
		sp1.port       == sp2.port       &&
		sp1.id_vendor  == sp2.id_vendor  &&
		sp1.id_product == sp2.id_product &&
		sp1.is_printer == sp2.is_printer;
}

extern std::vector<std::string> 	scan_serial_ports();
extern std::vector<SerialPortInfo> 	scan_serial_ports_extended();


class Serial : public boost::asio::serial_port
{
public:
	Serial(boost::asio::io_service &io_service);
	Serial(boost::asio::io_service &io_service, const std::string &name, unsigned baud_rate);
	Serial(const Serial &) = delete;
	Serial &operator=(const Serial &) = delete;
	~Serial();

	void set_baud_rate(unsigned baud_rate);

};


}
}

#endif
