#ifndef slic3r_Bonjour_hpp_
#define slic3r_Bonjour_hpp_

#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <boost/optional/optional.hpp>
#include <cstddef>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/io_service.hpp>
#include <cassert>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <set>
#include <unordered_map>
#include <functional>

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <utility>

namespace boost::asio::ip { class address_v4; }
namespace boost::asio::ip { class address_v6; }

namespace Slic3r {



struct BonjourReply
{
	typedef std::unordered_map<std::string, std::string> TxtData;

	boost::asio::ip::address ip;
	uint16_t port;
	std::string service_name;
	std::string hostname;
	std::string full_address;

	TxtData txt_data;

	BonjourReply() = delete;
	BonjourReply(boost::asio::ip::address ip,
		uint16_t port,
		std::string service_name,
		std::string hostname,
		TxtData txt_data);

	std::string path() const;

	bool operator==(const BonjourReply &other) const;
	bool operator<(const BonjourReply &other) const;
};

std::ostream& operator<<(std::ostream &, const BonjourReply &);

class Bonjour : public std::enable_shared_from_this<Bonjour> {
private:
	struct priv;
public:
	typedef std::shared_ptr<Bonjour> Ptr;
	typedef std::function<void(BonjourReply &&)> ReplyFn;
	typedef std::function<void()> CompleteFn;
	typedef std::function<void(const std::vector<BonjourReply>&)> ResolveFn;
	typedef std::set<std::string> TxtKeys;

	Bonjour(std::string service);
	Bonjour(Bonjour &&other);
	~Bonjour();

	Bonjour& set_protocol(std::string protocol);
	Bonjour& set_txt_keys(TxtKeys txt_keys);
	Bonjour& set_timeout(unsigned timeout);
	Bonjour& set_retries(unsigned retries);
	Bonjour& set_hostname(const std::string& hostname);

	Bonjour& on_reply(ReplyFn fn);
	Bonjour& on_complete(CompleteFn fn);

	Bonjour& on_resolve(ResolveFn fn);
	Ptr lookup();
	Ptr resolve();
	void resolve_sync();
private:
	std::unique_ptr<priv> p;
};

struct BonjourRequest
{
	static const boost::asio::ip::address_v4 MCAST_IP4;
	static const boost::asio::ip::address_v6 MCAST_IP6;
	static const uint16_t MCAST_PORT;

	std::vector<char> m_data;

	static boost::optional<BonjourRequest> make_PTR(const std::string& service, const std::string& protocol);
	static boost::optional<BonjourRequest> make_A(const std::string& hostname);
	static boost::optional<BonjourRequest> make_AAAA(const std::string& hostname);
private:
	BonjourRequest(std::vector<char>&& data) : m_data(std::move(data)) {}
};


class LookupSocket;
class ResolveSocket;

class UdpSession 
{
public:
	UdpSession(Bonjour::ReplyFn rfn);
	virtual ~UdpSession() = default;
	virtual void handle_receive(const boost::system::error_code& error, size_t bytes) = 0;
	std::vector<char>				buffer;
	boost::asio::ip::udp::endpoint	remote_endpoint;
protected:
	Bonjour::ReplyFn				replyfn;
};
typedef std::shared_ptr<UdpSession> SharedSession;
class LookupSession : public UdpSession 
{
public:
	LookupSession(const LookupSocket* sckt, Bonjour::ReplyFn rfn) : UdpSession(rfn), socket(sckt) {}
	void handle_receive(const  boost::system::error_code& error, size_t bytes) override;
protected:
	const LookupSocket* socket;
};
class ResolveSession : public UdpSession 
{
public:
	ResolveSession(const ResolveSocket* sckt, Bonjour::ReplyFn rfn) : UdpSession(rfn), socket(sckt) {}
	void handle_receive(const  boost::system::error_code& error, size_t bytes) override;
protected:
	const ResolveSocket* socket;
};

class UdpSocket
{
public:
	UdpSocket(Bonjour::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, const boost::asio::ip::address& interface_address
		, std::shared_ptr< boost::asio::io_service > io_service);

	UdpSocket(Bonjour::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, std::shared_ptr< boost::asio::io_service > io_service);

	void send();
	void async_receive();
	void cancel() { socket.cancel(); }
protected:
	void receive_handler(SharedSession session, const boost::system::error_code& error, size_t bytes);
	virtual SharedSession create_session() const = 0;

	Bonjour::ReplyFn								replyfn;
	boost::asio::ip::address					    multicast_address;
	boost::asio::ip::udp::socket					socket;
	boost::asio::ip::udp::endpoint					mcast_endpoint;
	std::shared_ptr< boost::asio::io_service >	io_service;
	std::vector<BonjourRequest>						requests;
};

class LookupSocket : public UdpSocket
{
public:
	LookupSocket(Bonjour::TxtKeys txt_keys
		, std::string service
		, std::string service_dn
		, std::string protocol
		, Bonjour::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, const boost::asio::ip::address& interface_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpSocket(replyfn, multicast_address, interface_address, io_service)
		, txt_keys(txt_keys)
		, service(service)
		, service_dn(service_dn)
		, protocol(protocol)
	{
		assert(!service.empty() && replyfn);
		create_request();
	}

	LookupSocket(Bonjour::TxtKeys txt_keys
		, std::string service
		, std::string service_dn
		, std::string protocol
		, Bonjour::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpSocket(replyfn, multicast_address, io_service)
		, txt_keys(txt_keys)
		, service(service)
		, service_dn(service_dn)
		, protocol(protocol)
	{
		assert(!service.empty() && replyfn);
		create_request();
	}

	const Bonjour::TxtKeys		get_txt_keys()   const { return txt_keys; }
	const std::string			get_service()    const { return service; }
	const std::string			get_service_dn() const { return service_dn; }

protected:
	SharedSession create_session() const override;
	void		  create_request()
	{
		requests.clear();
		if (auto rqst = BonjourRequest::make_PTR(service, protocol); rqst)
			requests.push_back(std::move(rqst.get()));
	}
	boost::optional<BonjourRequest> request;
	Bonjour::TxtKeys				txt_keys;
	std::string						service;
	std::string						service_dn;
	std::string						protocol;
};

class ResolveSocket : public UdpSocket
{
public:
	ResolveSocket(const std::string& hostname
		, Bonjour::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, const boost::asio::ip::address& interface_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpSocket(replyfn, multicast_address, interface_address, io_service)
		, hostname(hostname)

	{
		assert(!hostname.empty() && replyfn);
		create_requests();
	}

	ResolveSocket(const std::string& hostname
		, Bonjour::ReplyFn replyfn
		, const boost::asio::ip::address& multicast_address
		, std::shared_ptr< boost::asio::io_service > io_service)
		: UdpSocket(replyfn, multicast_address, io_service)
		, hostname(hostname)

	{
		assert(!hostname.empty() && replyfn);
		create_requests();
	}

	std::string get_hostname() const { return hostname; }
protected:
	SharedSession create_session() const override;
	void		  create_requests()
	{
		requests.clear();
		std::string trimmed_hostname = hostname;
		if (size_t dot_pos = trimmed_hostname.find_first_of('.'); dot_pos != std::string::npos)
			trimmed_hostname = trimmed_hostname.substr(0, dot_pos);
		if (auto rqst = BonjourRequest::make_A(trimmed_hostname); rqst)
			requests.push_back(std::move(rqst.get()));

		trimmed_hostname = hostname;
		if (size_t dot_pos = trimmed_hostname.find_first_of('.'); dot_pos != std::string::npos)
			trimmed_hostname = trimmed_hostname.substr(0, dot_pos);
		if (auto rqst = BonjourRequest::make_AAAA(trimmed_hostname); rqst)
			requests.push_back(std::move(rqst.get()));
	}

	std::string hostname;
};

}

#endif
