#define _POSIX_C_SOURCE 200809L
#include "web_control.h"
#include "face.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HTTP_PORT 8080
#define REQUEST_SIZE 2048
#define REQUEST_TIMEOUT_MS 1500
#define CLIENT_SLOTS 4
#define REPLY_SIZE 8192

static volatile sig_atomic_t shutdown_requested;
static atomic_int face_shutdown;

static void *face_worker(void *opaque)
{
    web_control_t *control = opaque;
    oled_t oled = {.fd = -1};
    face_t face = {0};
    if (oled_init(&oled, "/dev/i2c-3", OLED_DEFAULT_ADDRESS) < 0) {
        perror("web: OLED init");
        return NULL;
    }
    if (face_init(&face, &oled) < 0 || face_set(&face, FACE_NEUTRAL) < 0) {
        perror("web: neutral face");
        goto done;
    }
    face_id_t selected = FACE_NEUTRAL;
    while (!atomic_load(&face_shutdown)) {
        face_id_t target = (face_id_t)atomic_load(&control->face_target);
        if (target != selected) {
            if (face_set(&face, target) < 0) {
                perror("web: face set");
                break;
            }
            selected = target;
        }
        if (face_update(&face) < 0) {
            perror("web: face update");
            break;
        }
        struct timespec slice = {.tv_nsec = 40000000L};
        nanosleep(&slice, NULL);
    }
done:
    face_close(&face);
    if (oled_close(&oled) < 0) perror("web: OLED close");
    return NULL;
}

typedef struct {
    int fd;
    size_t used, sent, reply_size;
    int64_t deadline;
    char request[REQUEST_SIZE + 1], reply[REPLY_SIZE];
} client_t;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverlength-strings"
static const char page[] =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Quadruped Robot</title><style>"
    ":root{font-family:system-ui;color:#eef6fa;background:#0d1926}*{box-sizing:border-box}"
    "body{margin:0;min-height:100dvh;display:grid;place-items:center;padding:16px}"
    "main{width:min(100%,460px);background:#193041;border-radius:22px;padding:24px}"
    "button{font:inherit;font-weight:700;color:white;background:#276c81;border:0;border-radius:14px;"
    "min-height:68px;width:100%;margin:5px 0;touch-action:manipulation}"
    ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.stop{grid-column:1/-1;background:#c44854}"
    ".lang{text-align:right;font-size:14px}.lang a{color:inherit;text-decoration:none}"
    ".lang a[aria-current='true']{font-weight:700}"
    "#status{padding:10px;background:#102533;border-radius:10px}#message{color:#ffdc9c}"
    "</style></head><body><main><nav class='lang' aria-label='Language'>"
    "<a href='#' data-lang='en' aria-current='true'>English</a> / "
    "<a href='#' data-lang='zh'>中文</a></nav><h1>Quadruped Robot</h1>"
    "<p id='hint'>Lift the robot off the ground before its first stand.</p>"
    "<div id='status'>Status: Connecting</div>"
    "<button data-action='stand'>Initialize Stand</button><div class='grid'>"
    "<button data-action='turn-left'>Turn Left</button><button data-action='forward'>Forward</button>"
    "<button data-action='turn-right'>Turn Right</button><button data-action='hello-left'>Wave Left</button>"
    "<button data-action='backward'>Backward</button><button data-action='hello-right'>Wave Right</button>"
    "<button data-action='dance'>Dance</button>"
    "<button data-action='dance2'>Dance 2</button>"
    "<button data-action='sit'>Sit</button>"
    "<button class='stop' data-action='stop'>Stop</button></div><p id='message'></p>"
    "</main><script>"
    "const status=document.querySelector('#status'),msg=document.querySelector('#message');"
    "const text={en:{title:'Quadruped Robot',hint:'Lift the robot off the ground before its first stand.',"
    "status:'Status: ',action:'Action: ',next:'Next action: ',error:'Error code: ',"
    "connecting:'Connecting',sending:'Sending request, awaiting status confirmation',"
    "accepted:'Request accepted',"
    "failed:'Operation failed: ',states:{uninitialized:'Not initialized',"
    "initializing:'Initializing stand','waiting-initialization':'Stop requested; waiting for initialization',"
    "idle:'Stopped and restored',running:'Moving',stopping:'Stopping and restoring',"
    "restoring:'Restoring stand',error:'Error; motion disabled'},"
    "actions:{stand:'Initialize Stand',forward:'Forward',backward:'Backward',"
    "'turn-left':'Turn Left','turn-right':'Turn Right','hello-left':'Wave Left',"
    "'hello-right':'Wave Right',dance:'Dance',dance2:'Dance 2',sit:'Sit',stop:'Stop'}},"
    "zh:{title:'四足机器人',hint:'首次站立请将机器人抬起脱离地面',"
    "status:'状态：',action:'动作：',next:'下一动作：',error:'错误码：',"
    "connecting:'连接中',sending:'请求发送中，等待状态确认',accepted:'请求已接收',"
    "failed:'操作失败：',states:{uninitialized:'未初始化',initializing:'正在站立初始化',"
    "'waiting-initialization':'停止请求已收到，等待初始化完成',idle:'已停止并恢复完成',"
    "running:'运动中',stopping:'停止请求已收到，正在停止/恢复',"
    "restoring:'正在恢复站姿',error:'错误，禁止运动'},"
    "actions:{stand:'站立初始化',forward:'前进',backward:'后退',"
    "'turn-left':'左转','turn-right':'右转','hello-left':'左手招呼',"
    "'hello-right':'右手招呼',dance:'跳舞',dance2:'跳舞2',sit:'坐下',stop:'停止'}}};"
    "let latest=0,lang='en',shown=null,statusKey='connecting',messageKey='',detail='';"
    "function render(){const t=text[lang];document.documentElement.lang=lang==='en'?'en':'zh-CN';"
    "document.title=t.title;document.querySelector('h1').textContent=t.title;"
    "document.querySelector('#hint').textContent=t.hint;"
    "document.querySelectorAll('button[data-action]').forEach(b=>b.textContent=t.actions[b.dataset.action]);"
    "document.querySelectorAll('.lang a').forEach(a=>a.setAttribute('aria-current',a.dataset.lang===lang));"
    "status.textContent=statusKey==='requesting'?t.status+t.sending:shown?"
    "t.status+(t.states[shown.state]||shown.state)+' · '+t.action+"
    "(t.actions[shown.action]||shown.action)+"
    "(shown.state==='stopping'&&shown.pending!=='stop'?' · '+t.next+"
    "(t.actions[shown.pending]||shown.pending):'')+"
    "(shown.error?' · '+t.error+shown.error:''):t.status+t.connecting;"
    "msg.textContent=messageKey==='failed'?t.failed+detail:"
    "messageKey==='error'?detail:messageKey?t[messageKey]:'';}"
    "document.querySelectorAll('.lang a').forEach(a=>a.onclick=e=>{"
    "e.preventDefault();lang=a.dataset.lang;render();});"
    "async function query(){const id=++latest;try{"
    "const r=await fetch('/api/status',{cache:'no-store'});"
    "if(!r.ok)throw Error('HTTP '+r.status);const s=await r.json();if(id!==latest)return;"
    "shown=s;statusKey='';render();}catch(e){if(id===latest){messageKey='error';"
    "detail=e.message;render();}}}"
    "document.querySelectorAll('button').forEach(b=>b.onclick=async()=>{"
    "++latest;statusKey='requesting';messageKey='sending';render();"
    "try{const r=await fetch('/api/'+b.dataset.action,{method:'POST'});"
    "if(!r.ok)throw Error('HTTP '+r.status);messageKey='accepted';render();"
    "await query();}catch(e){messageKey='failed';detail=e.message;render();}});"
    "query();setInterval(query,1200);</script></body></html>";
#pragma GCC diagnostic pop

static void shutdown_signal(int number)
{
    (void)number;
    shutdown_requested = 1;
}

static int64_t now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void close_client(client_t *client)
{
    if (client->fd >= 0) close(client->fd);
    client->fd = -1;
}

static int respond(client_t *client, int code, const char *reason, const char *type,
                   const char *body)
{
    int n = snprintf(client->reply, sizeof(client->reply), "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n"
        "Cache-Control: no-store\r\n\r\n", code, reason, type, strlen(body));
    if (n < 0 || (size_t)n + strlen(body) > sizeof(client->reply)) {
        errno = EOVERFLOW; return -1;
    }
    memcpy(client->reply + n, body, strlen(body));
    client->reply_size = (size_t)n + strlen(body);
    client->sent = 0;
    int64_t now = now_ms();
    if (now < 0) return -1;
    client->deadline = now + REQUEST_TIMEOUT_MS;
    return 0;
}

static int status_response(client_t *client, int code, const char *reason,
                           web_control_t *control)
{
    web_status_t s = web_control_status(control);
    char json[160];
    int n = snprintf(json, sizeof(json),
        "{\"state\":\"%s\",\"action\":\"%s\",\"pending\":\"%s\",\"error\":%d}\n",
        web_state_name(s.state), web_action_name(s.action),
        web_action_name(s.pending), s.error);
    if (n < 0 || (size_t)n >= sizeof(json)) { errno = EOVERFLOW; return -1; }
    return respond(client, code, reason, "application/json; charset=utf-8", json);
}

static int route(client_t *client, const char *method, const char *path,
                 web_control_t *control)
{
    if (!strcmp(method, "GET") && !strcmp(path, "/"))
        return respond(client, 200, "OK", "text/html; charset=utf-8", page);
    if (!strcmp(method, "GET") && !strcmp(path, "/api/status"))
        return status_response(client, 200, "OK", control);
    if (strcmp(method, "POST"))
        return respond(client, 405, "Method Not Allowed", "text/plain", "Use POST\n");
    for (int action = WEB_STAND; action <= WEB_STOP; ++action) {
        char expected[40];
        snprintf(expected, sizeof(expected), "/api/%s", web_action_name((web_action_t)action));
        if (strcmp(path, expected)) continue;
        int result = web_control_request(control, (web_action_t)action);
        return status_response(client, result == 0 ? 202 : 409,
                               result == 0 ? "Accepted" : "Conflict", control);
    }
    return respond(client, 404, "Not Found", "text/plain", "Not Found\n");
}

/* Small fixed set of nonblocking sockets, not a thread pool or command queue. */
static int read_client(client_t *client, web_control_t *control)
{
    char method[8], path[96], version[16], extra;
    char *request = client->request;
    for (;;) {
        if (client->used == REQUEST_SIZE)
            return respond(client, 413, "Content Too Large", "text/plain", "Too large\n");
        ssize_t n = recv(client->fd, request + client->used,
                         REQUEST_SIZE - client->used, MSG_DONTWAIT);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (n <= 0) return -1;
        client->used += (size_t)n;
        if (memchr(request, '\0', client->used))
            return respond(client, 400, "Bad Request", "text/plain", "Bad Request\n");
        request[client->used] = '\0';
        char *end = strstr(request, "\r\n\r\n");
        if (!end) continue;
        char *line_end = strstr(request, "\r\n");
        if (!line_end || line_end - request >= 160)
            return respond(client, 400, "Bad Request", "text/plain", "Bad Request\n");
        *line_end = '\0';
        if (sscanf(request, "%7s %95s %15s %c", method, path, version, &extra) != 3 ||
            (strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1")))
            return respond(client, 400, "Bad Request", "text/plain", "Bad Request\n");
        /* No body is accepted: do not leave unread bytes to be interpreted as a command. */
        for (char *p = line_end + 2; p < end; ) {
            char *next = strstr(p, "\r\n");
            if (!next || next > end)
                return respond(client, 400, "Bad Request", "text/plain", "Bad Request\n");
            *next = '\0';
            if (!strncasecmp(p, "Transfer-Encoding:", 18) ||
                (!strncasecmp(p, "Content-Length:", 15) &&
                 strcmp(p + 15, " 0") && strcmp(p + 15, "0")))
                return respond(client, 400, "Bad Request", "text/plain", "No body\n");
            p = next + 2;
        }
        if (client->used != (size_t)(end + 4 - request))
            return respond(client, 400, "Bad Request", "text/plain", "No body\n");
        return route(client, method, path, control);
    }
}

static int write_client(client_t *client)
{
    while (client->sent < client->reply_size) {
        ssize_t n = send(client->fd, client->reply + client->sent,
                         client->reply_size - client->sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (n <= 0) return -1;
        client->sent += (size_t)n;
    }
    return 1;
}

int main(void)
{
    struct sigaction action = {0};
    action.sa_handler = shutdown_signal;
    if (sigemptyset(&action.sa_mask) < 0 ||
        sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) { perror("signal"); return 1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0), reuse = 1;
    if (fd < 0) { perror("socket"); return 1; }
    struct sockaddr_in address = {.sin_family = AF_INET,
        .sin_port = htons(HTTP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, 8) < 0) { perror("HTTP bind/listen"); close(fd); return 1; }
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("HTTP nonblocking"); close(fd); return 1;
    }
    /* Keep termination signals on the HTTP thread, not the servo worker. */
    sigset_t termination, previous;
    sigemptyset(&termination);
    sigaddset(&termination, SIGINT);
    sigaddset(&termination, SIGTERM);
    int mask_error = pthread_sigmask(SIG_BLOCK, &termination, &previous);
    if (mask_error) { errno = mask_error; perror("signal mask"); close(fd); return 1; }
    web_control_t control;
    if (web_control_init(&control) < 0) {
        perror("web controller"); close(fd);
        (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
        return 1;
    }
    atomic_store(&face_shutdown, 0);
    pthread_t face_thread;
    int face_thread_error = pthread_create(&face_thread, NULL, face_worker, &control);
    if (face_thread_error) {
        errno = face_thread_error;
        perror("web: OLED thread");
    }
    (void)pthread_sigmask(SIG_SETMASK, &previous, NULL);
    printf("quadruped_web listening on 0.0.0.0:8080\n");
    fflush(stdout);
    int result = 0;
    client_t clients[CLIENT_SLOTS] = {0};
    for (int i = 0; i < CLIENT_SLOTS; ++i) clients[i].fd = -1;
    while (!shutdown_requested) {
        struct pollfd watched[CLIENT_SLOTS + 1] = {{.fd = fd, .events = POLLIN}};
        for (int i = 0; i < CLIENT_SLOTS; ++i) {
            watched[i + 1].fd = clients[i].fd;
            watched[i + 1].events = clients[i].reply_size ? POLLOUT : POLLIN;
        }
        int ready = poll(watched, CLIENT_SLOTS + 1, 100);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (watched[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            perror("HTTP poll"); result = 1; break;
        }
        int64_t now = now_ms();
        if (now < 0) { perror("HTTP clock"); result = 1; break; }
        for (int i = 0; i < CLIENT_SLOTS; ++i) {
            client_t *client = &clients[i];
            if (client->fd < 0) continue;
            if (now >= client->deadline ||
                (watched[i + 1].revents & (POLLERR | POLLNVAL))) {
                close_client(client);
                continue;
            }
            if (watched[i + 1].revents & (client->reply_size ? POLLOUT : POLLIN)) {
                int step = client->reply_size ? write_client(client) : read_client(client, &control);
                if (step < 0 || (client->reply_size && step > 0)) close_client(client);
            }
            if (client->fd >= 0 && !client->reply_size &&
                (watched[i + 1].revents & POLLHUP) &&
                !(watched[i + 1].revents & POLLIN))
                close_client(client);
        }
        if (shutdown_requested || !(watched[0].revents & POLLIN)) continue;
        int incoming = accept(fd, NULL, NULL);
        if (incoming < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (incoming < 0) { perror("HTTP accept"); result = 1; break; }
        int slot = 0;
        while (slot < CLIENT_SLOTS && clients[slot].fd >= 0) ++slot;
        if (slot == CLIENT_SLOTS) {
            /* A newer request (especially STOP) must not wait behind stale readers. */
            slot = 0;
            for (int i = 1; i < CLIENT_SLOTS; ++i)
                if (clients[i].deadline < clients[slot].deadline) slot = i;
            close_client(&clients[slot]);
        }
        flags = fcntl(incoming, F_GETFL);
        if (flags < 0 || fcntl(incoming, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(incoming); continue;
        }
        clients[slot] = (client_t){.fd = incoming, .deadline = now + REQUEST_TIMEOUT_MS};
    }
    for (int i = 0; i < CLIENT_SLOTS; ++i) close_client(&clients[i]);
    close(fd);
    if (!face_thread_error) {
        atomic_store(&face_shutdown, 1);
        pthread_join(face_thread, NULL);
    }
    web_control_close(&control);
    return result;
}
