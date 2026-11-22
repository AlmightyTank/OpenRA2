// Package admin implements the opt-in, localhost-only operator dashboard.
// No third-party packages, cookies or persistent browser credentials.
package admin

import (
    "crypto/sha256"
    "crypto/subtle"
    "embed"
    "encoding/json"
    "errors"
    "io"
    "io/fs"
    "net"
    "net/http"
    "net/url"
    "strings"
    "time"

    "ra2lobbyd/internal/lobby"
    "ra2lobbyd/internal/relay"
    "ra2lobbyd/internal/tunnel"
)

//go:embed static/dashboard.html static/dashboard.css static/dashboard.js
var assets embed.FS

// LoopbackOnly refuses external or wildcard admin listeners, even if an
// operator accidentally supplies --admin=0.0.0.0:8080.
func LoopbackOnly(addr string) bool {
    host,_,err:=net.SplitHostPort(addr)
    if err!=nil {return false}
    if host=="localhost" {return true}
    ip:=net.ParseIP(host)
    return ip!=nil && ip.IsLoopback()
}

type Handler struct {
    server *lobby.Server
    v2 *tunnel.V2
    v3 *tunnel.V3
    pool *relay.Pool
    tokenHash [32]byte
    started time.Time
}

// New requires an explicit, high-entropy token from RA2_ADMIN_TOKEN.
// The token is never written to logs or rendered into HTML.
func New(server *lobby.Server,v2 *tunnel.V2,v3 *tunnel.V3,pool *relay.Pool,token string)(*Handler,error){
    if server==nil || len(token)<32 {
        return nil,errors.New("admin dashboard requires a server and RA2_ADMIN_TOKEN of at least 32 bytes")
    }
    return &Handler{server:server,v2:v2,v3:v3,pool:pool,tokenHash:sha256.Sum256([]byte(token)),started:time.Now()},nil
}

func localRequest(r *http.Request) bool {
    host,_,err:=net.SplitHostPort(r.Host)
    if err!=nil{return false}
    ip:=net.ParseIP(host)
    if host!="localhost" && (ip==nil || !ip.IsLoopback()) {return false}
    ipStr,_,err:=net.SplitHostPort(r.RemoteAddr)
    if err!=nil{return false}
    remote:=net.ParseIP(ipStr)
    return remote!=nil && remote.IsLoopback()
}

func (h *Handler) authorized(r *http.Request) bool {
    header:=r.Header.Get("Authorization")
    if !strings.HasPrefix(header,"Bearer ") {return false}
    token:=strings.TrimPrefix(header,"Bearer ")
    if token=="" || len(token)>1024 {return false}
    hash:=sha256.Sum256([]byte(token))
    return subtle.ConstantTimeCompare(hash[:],h.tokenHash[:])==1
}

func validOrigin(r *http.Request) bool {
    origin:=r.Header.Get("Origin")
    if origin=="" { return true } // curl and other trusted local CLI clients
    u,err:=url.Parse(origin)
    return err==nil && u.Scheme=="http" && u.Host==r.Host && u.User==nil && u.Path==""
}

func noCache(w http.ResponseWriter) {
    w.Header().Set("Cache-Control","no-store")
    w.Header().Set("X-Content-Type-Options","nosniff")
    w.Header().Set("X-Frame-Options","DENY")
    w.Header().Set("Referrer-Policy","no-referrer")
    w.Header().Set("Content-Security-Policy","default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'")
}

func jsonResponse(w http.ResponseWriter,status int,v any) {
    w.Header().Set("Content-Type","application/json; charset=utf-8")
    w.WriteHeader(status)
    _=json.NewEncoder(w).Encode(v)
}

func (h *Handler) ServeHTTP(w http.ResponseWriter,r *http.Request) {
    noCache(w)
    if !localRequest(r) {http.Error(w,"operator panel accepts loopback connections only",http.StatusForbidden);return}
    switch r.URL.Path {
    case "/admin/","/admin/dashboard.css","/admin/dashboard.js":
        if r.Method!=http.MethodGet && r.Method!=http.MethodHead {
            w.Header().Set("Allow","GET, HEAD")
            http.Error(w,"method not allowed",http.StatusMethodNotAllowed)
            return
        }
        filename:="dashboard.html"
        contentType:="text/html; charset=utf-8"
        if r.URL.Path=="/admin/dashboard.css" { filename="dashboard.css";contentType="text/css; charset=utf-8" }
        if r.URL.Path=="/admin/dashboard.js" { filename="dashboard.js";contentType="text/javascript; charset=utf-8" }
        content,err:=fs.ReadFile(assets,"static/"+filename)
        if err!=nil {http.Error(w,"dashboard unavailable",http.StatusInternalServerError);return}
        w.Header().Set("Content-Type",contentType)
        if r.Method==http.MethodGet {_,_=w.Write(content)}
        return
    case "/admin/api/status","/admin/api/rooms":
        if !h.authorized(r) {w.Header().Set("WWW-Authenticate",`Bearer realm="OpenRA2 admin"`);jsonResponse(w,http.StatusUnauthorized,map[string]string{"error":"unauthorized"});return}
        if r.Method!=http.MethodGet {
            w.Header().Set("Allow","GET")
            jsonResponse(w,http.StatusMethodNotAllowed,map[string]string{"error":"method_not_allowed"});return
        }
        if r.URL.Path=="/admin/api/status" {h.status(w);return}
        jsonResponse(w,http.StatusOK,map[string]any{"rooms":h.server.AdminRooms()})
        return
    }
    // Waiting-room moderation is the only mutation in S0.1E. It requires
    // a bearer token in a request header and no user-supplied JSON body.
    if strings.HasPrefix(r.URL.Path,"/admin/api/rooms/") &&
        strings.HasSuffix(r.URL.Path,"/close") {
        if !h.authorized(r) {jsonResponse(w,http.StatusUnauthorized,map[string]string{"error":"unauthorized"});return}
        if r.Method!=http.MethodPost {
            w.Header().Set("Allow","POST")
            jsonResponse(w,http.StatusMethodNotAllowed,map[string]string{"error":"method_not_allowed"});return
        }
        if !validOrigin(r) {jsonResponse(w,http.StatusForbidden,map[string]string{"error":"bad_origin"});return}
        if r.ContentLength>0 || r.TransferEncoding!=nil {
            // Reject arbitrary bodies to keep the mutating API narrow.
            io.Copy(io.Discard,io.LimitReader(r.Body,1024))
            jsonResponse(w,http.StatusBadRequest,map[string]string{"error":"body_not_allowed"});return
        }
        path:=strings.TrimSuffix(strings.TrimPrefix(r.URL.Path,"/admin/api/rooms/"),"/close")
        if strings.Contains(path,"/") || !lobby.ValidCode(path) {
            jsonResponse(w,http.StatusBadRequest,map[string]string{"error":"bad_room_code"});return
        }
        err:=h.server.AdminCloseWaitingRoom(path)
        switch {
        case errors.Is(err,lobby.ErrRoomNotFound):
            jsonResponse(w,http.StatusNotFound,map[string]string{"error":"room_not_found"})
        case errors.Is(err,lobby.ErrRoomStarted):
            jsonResponse(w,http.StatusConflict,map[string]string{"error":"started_match_cannot_be_closed"})
        case err!=nil:
            jsonResponse(w,http.StatusInternalServerError,map[string]string{"error":"close_failed"})
        default:
            jsonResponse(w,http.StatusOK,map[string]string{"status":"closed"})
        }
        return
    }
    http.NotFound(w,r)
}

func (h *Handler) status(w http.ResponseWriter) {
    stats:=h.server.Stats()
    v3clients:=0
    var forwarded,dropped,bytes uint64
    if h.v3!=nil {
        v3clients=h.v3.Clients()
        forwarded=h.v3.Stats.Forwarded.Load()
        dropped=h.v3.Stats.Dropped.Load()
        bytes=h.v3.Stats.Bytes.Load()
    }
    allocations:=0
    if h.pool!=nil {allocations=h.pool.InUse()}
    v2used:=0
    if h.v2!=nil {_,v2used=h.v2.Slots()}
    jsonResponse(w,http.StatusOK,map[string]any{
        "status":"healthy", "uptime_seconds":int64(time.Since(h.started).Seconds()),
        "rooms":stats,
        "connections_total":h.server.Metrics.Conns.Load(),
        "joins_total":h.server.Metrics.Joins.Load(),
        "join_failures_total":h.server.Metrics.JoinFails.Load(),
        "v3_clients":v3clients, "v3_forwarded_total":forwarded,
        "v3_dropped_total":dropped, "v3_bytes_total":bytes,
        "v2_slots_used":v2used, "port_relay_allocations":allocations,
    })
}

// Compile-time check that the UI's endpoint is an HTTP handler.
var _ http.Handler = (*Handler)(nil)
