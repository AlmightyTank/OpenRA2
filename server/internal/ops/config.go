// Package ops validates server configuration before opening live sockets.
package ops

import (
    "fmt"
    "net"
    "strconv"
    "strings"
)

type Config struct {
    Listen, V3, Admin, RelayPorts, PublicIP, AdminToken string
    MaxRooms, MaxRoomsPerIP, MaxConnsPerIP, TunnelClients, TunnelIPLimit int
}

func address(addr string, optional bool) error {
    if addr=="" && optional {return nil}
    host,port,err:=net.SplitHostPort(addr)
    if err!=nil {return fmt.Errorf("invalid host:port %q",addr)}
    _=host
    n,err:=strconv.Atoi(port)
    if err!=nil || n<0 || n>65535 {return fmt.Errorf("invalid port in %q",addr)}
    return nil
}
func loopbackOnly(addr string) bool {
    host,_,err:=net.SplitHostPort(addr)
    if err!=nil {return false}
    if host=="localhost" {return true}
    ip:=net.ParseIP(host)
    return ip!=nil && ip.IsLoopback()
}
func portRange(value string) error {
    if value=="" {return nil}
    p:=strings.Split(value,"-")
    if len(p)!=2{return fmt.Errorf("relay port range must be LO-HI")}
    lo,e1:=strconv.Atoi(p[0])
    hi,e2:=strconv.Atoi(p[1])
    if e1!=nil || e2!=nil || lo<1024 || hi>65535 || hi<lo {
        return fmt.Errorf("invalid relay port range")
    }
    return nil
}

// Validate permits 0 (ephemeral port) for disposable test listeners, but
// rejects unsafe admin bindings, malformed ports and invalid capacity settings.
// Never echo the operator token in an error or log.
func Validate(c Config) error {
    if err:=address(c.Listen,false);err!=nil{return fmt.Errorf("listen: %w",err)}
    if err:=address(c.V3,true);err!=nil{return fmt.Errorf("v3: %w",err)}
    if c.Admin!="" {
        if err:=address(c.Admin,false);err!=nil{return fmt.Errorf("admin: %w",err)}
        if !loopbackOnly(c.Admin){return fmt.Errorf("admin listener must bind to loopback")}
    }
    if c.AdminToken!="" && len(c.AdminToken)<32 {
        return fmt.Errorf("admin token must contain at least 32 bytes")
    }
    if err:=portRange(c.RelayPorts);err!=nil{return err}
    if c.PublicIP!="" {
        ip:=net.ParseIP(c.PublicIP)
        if ip==nil || ip.To4()==nil || ip.IsUnspecified() {
            return fmt.Errorf("public IP must be a usable IPv4 literal")
        }
    }
    if c.MaxRooms<=0 || c.MaxRoomsPerIP<=0 || c.MaxConnsPerIP<=0 ||
        c.TunnelClients<=0 || c.TunnelIPLimit<=0 {
        return fmt.Errorf("room, connection and tunnel limits must be positive")
    }
    return nil
}
