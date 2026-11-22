package ops

import (
 "strings"
 "testing"
)

func validConfig() Config {
 return Config{
  Listen:":50000",V3:":50001",Admin:"127.0.0.1:8080",
  RelayPorts:"50100-50999",MaxRooms:500,MaxRoomsPerIP:3,
  MaxConnsPerIP:5,TunnelClients:200,TunnelIPLimit:8,
 }
}
func TestValidateDefaultsAndDisposableTestPorts(t *testing.T) {
 c:=validConfig()
 if err:=Validate(c);err!=nil {t.Fatal(err)}
 c.Listen="127.0.0.1:0";c.V3="127.0.0.1:0";c.Admin=""
 c.RelayPorts=""
 if err:=Validate(c);err!=nil {t.Fatalf("valid ephemeral test setup: %v",err)}
}
func TestValidateRejectsUnsafeDeployConfiguration(t *testing.T) {
 cases:=map[string]func(*Config){
 "wildcard admin":func(c *Config){c.Admin="0.0.0.0:8080"},
 "wildcard v6 admin":func(c *Config){c.Admin="[::]:8080"},
 "nonloopback admin":func(c *Config){c.Admin="192.168.0.5:8080"},
 "malformed admin":func(c *Config){c.Admin="localhost"},
 "short token":func(c *Config){c.AdminToken="notlongenough"},
 "bad listen":func(c *Config){c.Listen="bad"},
 "bad v3":func(c *Config){c.V3="127.0.0.1:notaport"},
 "invalid range":func(c *Config){c.RelayPorts="1-65000"},
 "backwards range":func(c *Config){c.RelayPorts="50999-50100"},
 "unusable public IP":func(c *Config){c.PublicIP="0.0.0.0"},
 "hostname public IP":func(c *Config){c.PublicIP="example.com"},
 "no rooms":func(c *Config){c.MaxRooms=0},
 "no per-IP capacity":func(c *Config){c.MaxConnsPerIP=0},
 "no tunnel capacity":func(c *Config){c.TunnelClients=0},
 }
 for name,change:=range cases {
  t.Run(name,func(t *testing.T){
   c:=validConfig();change(&c)
   if err:=Validate(c);err==nil{t.Fatal("unsafe config accepted")}
  })
 }
 c:=validConfig()
 c.AdminToken=strings.Repeat("secure-random-string",3)
 if err:=Validate(c);err!=nil{t.Fatalf("strong token refused: %v",err)}
}
func TestInvalidConfigErrorNeverLeaksSecret(t *testing.T) {
 c:=validConfig()
 c.AdminToken="a-secret-token"
 err:=Validate(c)
 if err==nil || strings.Contains(err.Error(),c.AdminToken) {
  t.Fatalf("invalid admin token should fail without disclosing the secret")
 }
}
