package admin

import (
    "encoding/json"
    "net/http"
    "net/http/httptest"
    "strings"
    "testing"

    "ra2lobbyd/internal/lobby"
)

const strongToken = "test-only-operator-secret-for-admin-auth-123456789"

func request(h http.Handler, method,path,token,origin string) *httptest.ResponseRecorder {
    req:=httptest.NewRequest(method,"http://127.0.0.1:8080"+path,nil)
    req.RemoteAddr="127.0.0.1:32132"
    if token!="" {req.Header.Set("Authorization","Bearer "+token)}
    if origin!="" {req.Header.Set("Origin",origin)}
    rec:=httptest.NewRecorder()
    h.ServeHTTP(rec,req)
    return rec
}

func TestLoopbackOnlyListener(t *testing.T) {
    valid:=[]string{"127.0.0.1:8080","localhost:8080","[::1]:8080"}
    invalid:=[]string{"0.0.0.0:8080",":8080","[::]:8080","192.168.1.3:8080",
        "example.com:8080","127.0.0.1","bad"}
    for _,a:=range valid {if !LoopbackOnly(a){t.Errorf("refused loopback %q",a)}}
    for _,a:=range invalid {if LoopbackOnly(a){t.Errorf("accepted external/wildcard listener %q",a)}}
}

func TestDashboardRequiresExplicitStrongToken(t *testing.T){
    srv:=lobby.New(lobby.Config{})
    for _,token:=range []string{"", "short",strings.Repeat("x",31)}{
        if _,err:=New(srv,nil,nil,nil,token);err==nil {
            t.Fatalf("accepted short/empty admin token")
        }
    }
    if _,err:=New(srv,nil,nil,nil,strongToken);err!=nil {t.Fatal(err)}
}

func TestDashboardAuthAndPrivacyHeaders(t *testing.T) {
    srv:=lobby.New(lobby.Config{})
    h,err:=New(srv,nil,nil,nil,strongToken)
    if err!=nil {t.Fatal(err)}
    html:=request(h,"GET","/admin/","","")
    if html.Code!=http.StatusOK || !strings.Contains(html.Body.String(),"Server operations") {
        t.Fatalf("dashboard page unavailable: %d",html.Code)
    }
    if strings.Contains(html.Body.String(),strongToken){t.Fatal("embedded secret in page")}
    if html.Header().Get("Cache-Control")!="no-store" || html.Header().Get("Content-Security-Policy")=="" {
        t.Fatal("missing cache or CSP restrictions")
    }
    for _,path:=range []string{"/admin/dashboard.css","/admin/dashboard.js"} {
        if r:=request(h,"GET",path,"","");r.Code!=http.StatusOK {
            t.Fatalf("asset %s returned %d",path,r.Code)
        }
    }
    for _,path:=range []string{"/admin/api/status","/admin/api/rooms"}{
        if r:=request(h,"GET",path,"","");r.Code!=http.StatusUnauthorized {t.Fatalf("unauthenticated %s returned %d",path,r.Code)}
        if r:=request(h,"GET",path,"wrong-secret","");r.Code!=http.StatusUnauthorized {t.Fatalf("wrong secret %s returned %d",path,r.Code)}
        r:=request(h,"GET",path,strongToken,"")
        if r.Code!=http.StatusOK {t.Fatalf("authorized %s returned %d: %s",path,r.Code,r.Body.String())}
    }
    status:=request(h,"GET","/admin/api/status",strongToken,"")
    var decoded struct{
        Status string `json:"status"`
        Rooms lobby.RoomStats `json:"rooms"`
    }
    if err:=json.Unmarshal(status.Body.Bytes(),&decoded);err!=nil || decoded.Status!="healthy" || decoded.Rooms.Total!=0 {
        t.Fatalf("bad dashboard stats: %v %+v",err,decoded)
    }
    if r:=request(h,"POST","/admin/api/status",strongToken,"");r.Code!=http.StatusMethodNotAllowed {
        t.Fatalf("status accepted POST: %d",r.Code)
    }
}

func TestDashboardRejectsExternalClientsAndCrossSiteMutations(t *testing.T){
    h,_:=New(lobby.New(lobby.Config{}),nil,nil,nil,strongToken)
    req:=httptest.NewRequest("GET","http://127.0.0.1:8080/admin/api/rooms",nil)
    req.RemoteAddr="203.0.113.3:45612"
    req.Header.Set("Authorization","Bearer "+strongToken)
    rec:=httptest.NewRecorder()
    h.ServeHTTP(rec,req)
    if rec.Code!=http.StatusForbidden {t.Fatalf("accepted non-local request: %d",rec.Code)}
    req=httptest.NewRequest("GET","http://example.com:8080/admin/api/rooms",nil)
    req.RemoteAddr="127.0.0.1:1234"
    req.Header.Set("Authorization","Bearer "+strongToken)
    rec=httptest.NewRecorder()
    h.ServeHTTP(rec,req)
    if rec.Code!=http.StatusForbidden {t.Fatalf("accepted rebind Host: %d",rec.Code)}

    path:="/admin/api/rooms/AAAAAA/close"
    if r:=request(h,"POST",path,"","");r.Code!=http.StatusUnauthorized {t.Fatalf("unauthenticated close: %d",r.Code)}
    if r:=request(h,"GET",path,strongToken,"");r.Code!=http.StatusMethodNotAllowed {t.Fatalf("GET mutated room: %d",r.Code)}
    if r:=request(h,"POST",path,strongToken,"https://example.org");r.Code!=http.StatusForbidden {t.Fatalf("cross-origin close: %d",r.Code)}
    if r:=request(h,"POST",path,strongToken,"http://127.0.0.1:8080");r.Code!=http.StatusNotFound {t.Fatalf("valid auth+origin returned %d",r.Code)}
    if r:=request(h,"POST","/admin/api/rooms/INVALID-ROOM/close",strongToken,"");r.Code!=http.StatusBadRequest {t.Fatalf("bad code returned %d",r.Code)}
}
