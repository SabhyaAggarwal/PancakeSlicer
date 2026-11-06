package main

import (
	"context"
	"errors"
	"fmt"
	"net"
	"net/netip"
	"net/url"
	"strconv"
	"strings"
	"time"
)

type hostPolicy struct {
	prefixes []netip.Prefix
	names    []string
	ports    []uint16
}

var deniedPrefixes = []netip.Prefix{
	netip.MustParsePrefix("0.0.0.0/8"),
	netip.MustParsePrefix("169.254.0.0/16"),
	netip.MustParsePrefix("255.255.255.255/32"),
	netip.MustParsePrefix("fd00:ec2::254/128"),
}

var nat64 = netip.MustParsePrefix("64:ff9b::/96")

var lanPrefixes = []string{"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "100.64.0.0/10", "fc00::/7"}

func parseHostPolicy(list, listen string) (*hostPolicy, error) {
	if strings.TrimSpace(list) == "" {
		return nil, nil
	}
	h := &hostPolicy{}
	for _, entry := range strings.FieldsFunc(list, func(r rune) bool { return r == ',' || r == ' ' || r == '\n' || r == '\t' }) {
		if entry == "lan" {
			for _, p := range lanPrefixes {
				h.prefixes = append(h.prefixes, netip.MustParsePrefix(p))
			}
		} else if prefix, err := netip.ParsePrefix(entry); err == nil {
			h.prefixes = append(h.prefixes, unmapPrefix(prefix.Masked()))
		} else if ip, err := netip.ParseAddr(entry); err == nil {
			ip = ip.Unmap()
			h.prefixes = append(h.prefixes, netip.PrefixFrom(ip, ip.BitLen()))
		} else if name := strings.ToLower(strings.TrimSuffix(entry, ".")); validHostName(strings.TrimPrefix(name, ".")) {
			h.names = append(h.names, name)
		} else {
			return nil, fmt.Errorf("printer-network: %q is not lan, a CIDR, an address or a host name", entry)
		}
	}
	if _, port, err := net.SplitHostPort(listen); err == nil {
		if n, err := strconv.ParseUint(port, 10, 16); err == nil {
			h.ports = append(h.ports, uint16(n))
		}
	}
	return h, nil
}

func unmapPrefix(p netip.Prefix) netip.Prefix {
	if p.Addr().Is4In6() && p.Bits() >= 96 {
		return netip.PrefixFrom(p.Addr().Unmap(), p.Bits()-96)
	}
	return p
}

func validHostName(name string) bool {
	if name == "" || len(name) > 253 {
		return false
	}
	for _, label := range strings.Split(name, ".") {
		if label == "" || len(label) > 63 || label[0] == '-' || label[len(label)-1] == '-' {
			return false
		}
		for _, r := range label {
			if !(r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r == '-') {
				return false
			}
		}
	}
	return true
}

func (h *hostPolicy) String() string {
	var parts []string
	for _, p := range h.prefixes {
		parts = append(parts, p.String())
	}
	return strings.Join(append(parts, h.names...), ", ")
}

var errHostRefused = errors.New("this service does not allow printers at that address (its printer network setting)")

type hostRefusal struct{ reason string }

func (e *hostRefusal) Error() string { return errHostRefused.Error() }
func (e *hostRefusal) Unwrap() error { return errHostRefused }

func hostURL(typed string) (*url.URL, error) {
	typed = strings.TrimSpace(typed)
	if !strings.Contains(typed, "://") {
		typed = "http://" + typed
	}
	u, err := url.Parse(typed)
	if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Hostname() == "" || u.User != nil || u.RawQuery != "" || u.Fragment != "" || strings.Trim(u.Path, "/") != "" {
		return nil, errors.New("enter the printer's host name or IP address, optionally with :port (no path)")
	}
	if p := u.Port(); p != "" {
		if n, err := strconv.ParseUint(p, 10, 16); err != nil || n == 0 {
			return nil, errors.New("the port must be 1-65535")
		}
	}
	host := strings.ToLower(strings.TrimSuffix(u.Hostname(), "."))
	if _, err := netip.ParseAddr(host); err != nil && !validHostName(host) {
		return nil, errors.New("enter the printer's host name or IP address, optionally with :port (no path)")
	}
	return &url.URL{Scheme: u.Scheme, Host: u.Host}, nil
}

func urlPort(u *url.URL) uint16 {
	if p, err := strconv.ParseUint(u.Port(), 10, 16); err == nil {
		return uint16(p)
	}
	return map[string]uint16{"http": 80, "https": 443}[u.Scheme]
}

func (h *hostPolicy) resolve(ctx context.Context, u *url.URL) (netip.AddrPort, error) {
	port := urlPort(u)
	for _, own := range h.ports {
		if port == own {
			return netip.AddrPort{}, &hostRefusal{fmt.Sprintf("port %d is this service's own", port)}
		}
	}
	host := strings.ToLower(strings.TrimSuffix(u.Hostname(), "."))
	var addrs []netip.Addr
	if ip, err := netip.ParseAddr(host); err == nil {
		addrs = []netip.Addr{ip}
	} else {
		ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
		defer cancel()
		if addrs, err = net.DefaultResolver.LookupNetIP(ctx, "ip", host); err != nil || len(addrs) == 0 {
			return netip.AddrPort{}, errors.New("cannot resolve the printer's address")
		}
	}
	named := false
	for _, name := range h.names {
		named = named || host == name || strings.HasPrefix(name, ".") && strings.HasSuffix(host, name)
	}
	for _, ip := range addrs {
		if reason := h.check(ip.Unmap().WithZone(""), named); reason != "" {
			return netip.AddrPort{}, &hostRefusal{fmt.Sprintf("%s resolves to %s: %s", host, ip, reason)}
		}
	}
	return netip.AddrPortFrom(addrs[0].Unmap().WithZone(""), port), nil
}

func (h *hostPolicy) check(ip netip.Addr, named bool) string {
	if nat64.Contains(ip) {
		embedded := ip.As16()
		if reason := denied(netip.AddrFrom4([4]byte(embedded[12:]))); reason != "" {
			return "NAT64 of " + reason
		}
	}
	if reason := denied(ip); reason != "" {
		return reason
	}
	if named {
		return ""
	}
	for _, p := range h.prefixes {

		if p.Contains(ip) && (!ip.Is4() || p.Bits() >= 31 || ip != p.Addr() && ip != lastAddr(p)) {
			return ""
		}
	}
	return "not in the allowed list"
}

func denied(ip netip.Addr) string {
	switch {
	case ip.IsLoopback():
		return "loopback"
	case ip.IsUnspecified():
		return "unspecified"
	case ip.IsMulticast():
		return "multicast"
	case ip.IsLinkLocalUnicast():
		return "link-local"
	}
	for _, p := range deniedPrefixes {
		if p.Contains(ip) {
			return "reserved (" + p.String() + ")"
		}
	}
	return ""
}

func lastAddr(p netip.Prefix) netip.Addr {
	a := p.Addr().As4()
	host := uint32(1)<<(32-p.Bits()) - 1
	v := (uint32(a[0])<<24 | uint32(a[1])<<16 | uint32(a[2])<<8 | uint32(a[3])) | host
	return netip.AddrFrom4([4]byte{byte(v >> 24), byte(v >> 16), byte(v >> 8), byte(v)})
}
