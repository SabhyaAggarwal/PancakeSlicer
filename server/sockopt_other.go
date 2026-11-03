//go:build !unix

package main

import "syscall"

type socket = syscall.Handle

func reuseAddr(network, address string, c syscall.RawConn) error {
	return c.Control(func(fd uintptr) {
		syscall.SetsockoptInt(socket(fd), syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 1)
		syscall.SetsockoptInt(socket(fd), syscall.SOL_SOCKET, syscall.SO_BROADCAST, 1)
	})
}
