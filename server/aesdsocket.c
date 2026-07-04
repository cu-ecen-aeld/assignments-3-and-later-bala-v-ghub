#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT            "9000"
#define DATAFILE        "/var/tmp/aesdsocketdata"
#define BACKLOG         10
#define RECV_BUF        1024
#define TIMESTAMP_SECS  10

static int server_fd = -1;
static volatile sig_atomic_t caught_signal = 0;

static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

struct thread_entry {
    pthread_t          tid;
    int                client_fd;
    char               client_ip[INET6_ADDRSTRLEN];
    int                complete;
    SLIST_ENTRY(thread_entry) entries;
};

SLIST_HEAD(thread_list_head, thread_entry);
static struct thread_list_head thread_list = SLIST_HEAD_INITIALIZER(thread_list);
static pthread_mutex_t thread_list_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t timer_thread;
static int timer_thread_started = 0;

static void signal_handler(int signo)
{
    (void)signo;
    caught_signal = 1;
}

/* Send entire contents of DATAFILE back to the client. Caller must hold file_lock. */
static int send_file(int fd)
{
    FILE *f = fopen(DATAFILE, "r");
    if (!f)
    {
        return -1;
    }
    char buf[RECV_BUF];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        if (send(fd, buf, n, MSG_NOSIGNAL) == -1)
        {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

/* Handle one accepted connection: receive packets, append, send back. */
static void handle_connection(int fd, const char *client_ip)
{
    char   recv_buf[RECV_BUF];
    char  *packet   = NULL;
    size_t pkt_len  = 0;

    for (;;)
    {
        ssize_t n = recv(fd, recv_buf, sizeof(recv_buf), 0);
        if (n <= 0)
            break; /* connection closed or error */

        /* Grow packet buffer and append received bytes */
        char *tmp = realloc(packet, pkt_len + (size_t)n + 1);
        if (!tmp)
        {
            syslog(LOG_ERR, "realloc failed: %s", strerror(errno));
            free(packet);
            return;
        }
        packet = tmp;
        memcpy(packet + pkt_len, recv_buf, (size_t)n);
        pkt_len += (size_t)n;
        packet[pkt_len] = '\0';

        /* Process every complete newline-terminated packet */
        char *start = packet;
        char *nl;
        while ((nl = memchr(start, '\n', pkt_len - (size_t)(start - packet))) != NULL)
        {
            size_t line_len = (size_t)(nl - start) + 1; /* include newline */

            /* Append to file and echo back, atomically w.r.t. other threads */
            pthread_mutex_lock(&file_lock);
            FILE *f = fopen(DATAFILE, "a");
            if (!f)
            {
                syslog(LOG_ERR, "fopen %s: %s", DATAFILE, strerror(errno));
            }
            else
            {
                fwrite(start, 1, line_len, f);
                fclose(f);
                send_file(fd);
            }
            pthread_mutex_unlock(&file_lock);

            start = nl + 1;
        }

        /* Keep any incomplete (no newline yet) tail */
        size_t remaining = pkt_len - (size_t)(start - packet);
        if (remaining > 0)
            memmove(packet, start, remaining);
        pkt_len = remaining;
        packet[pkt_len] = '\0';
    }

    free(packet);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

static void *connection_thread_func(void *arg)
{
    struct thread_entry *entry = (struct thread_entry *)arg;

    handle_connection(entry->client_fd, entry->client_ip);

    close(entry->client_fd);
    entry->client_fd = -1;
    entry->complete = 1;
    return NULL;
}

/* Join and free any threads that have finished, removing them from the list. */
static void reap_completed_threads(void)
{
    pthread_mutex_lock(&thread_list_lock);

    struct thread_entry *entry = SLIST_FIRST(&thread_list);
    while (entry != NULL)
    {
        struct thread_entry *next = SLIST_NEXT(entry, entries);
        if (entry->complete)
        {
            pthread_join(entry->tid, NULL);
            SLIST_REMOVE(&thread_list, entry, thread_entry, entries);
            free(entry);
        }
        entry = next;
    }

    pthread_mutex_unlock(&thread_list_lock);
}

/* Join and free every remaining thread, regardless of completion state. */
static void join_all_threads(void)
{
    pthread_mutex_lock(&thread_list_lock);
    struct thread_entry *entry;
    while (!SLIST_EMPTY(&thread_list))
    {
        entry = SLIST_FIRST(&thread_list);
        pthread_mutex_unlock(&thread_list_lock);

        pthread_join(entry->tid, NULL);

        pthread_mutex_lock(&thread_list_lock);
        SLIST_REMOVE_HEAD(&thread_list, entries);
        free(entry);
    }
    pthread_mutex_unlock(&thread_list_lock);
}

static void *timer_thread_func(void *arg)
{
    (void)arg;
    while (!caught_signal)
    {
        for (int i = 0; i < TIMESTAMP_SECS && !caught_signal; i++)
            sleep(1);
        if (caught_signal)
            break;

        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);

        char ts_buf[128];
        strftime(ts_buf, sizeof(ts_buf), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", &tm_now);

        pthread_mutex_lock(&file_lock);
        FILE *f = fopen(DATAFILE, "a");
        if (!f)
        {
            syslog(LOG_ERR, "fopen %s: %s", DATAFILE, strerror(errno));
        }
        else
        {
            fwrite(ts_buf, 1, strlen(ts_buf), f);
            fclose(f);
        }
        pthread_mutex_unlock(&file_lock);
    }
    return NULL;
}

static void cleanup(void)
{
    join_all_threads();

    if (timer_thread_started)
        pthread_join(timer_thread, NULL);

    if (server_fd != -1)
    {
        close(server_fd);
        server_fd = -1;
    }
    if (unlink(DATAFILE) != 0 && errno != ENOENT)
        syslog(LOG_ERR, "unlink %s: %s", DATAFILE, strerror(errno));
    closelog();
}

static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
        exit(EXIT_SUCCESS); /* parent exits */

    if (setsid() < 0)
    {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    if (chdir("/") < 0)
    {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    /* Redirect stdin/stdout/stderr to /dev/null */
    int devnull = open("/dev/null", O_RDWR);
    if (devnull != -1)
    {
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
}

int main(int argc, char *argv[])
{
    int daemon_mode = 0;
    if (argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    /* Signal handlers */
    struct sigaction sa = { .sa_handler = signal_handler, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    /* Ignore SIGPIPE so send() returns EPIPE instead of killing the process
     * when a client closes the connection before we finish sending. */
    signal(SIGPIPE, SIG_IGN);

    /* Resolve address and create socket */
    struct addrinfo hints = {0};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    struct addrinfo *res;
    if (getaddrinfo(NULL, PORT, &hints, &res) != 0)
    {
        syslog(LOG_ERR, "getaddrinfo: %s", strerror(errno));
        return -1;
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) 
    {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
     {
        syslog(LOG_ERR, "setsockopt SO_REUSEADDR: %s", strerror(errno));
        freeaddrinfo(res);
        cleanup();
        return -1;
    }

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) == -1)
     {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        freeaddrinfo(res);
        cleanup();
        return -1;
    }
    freeaddrinfo(res);

    /* Fork into background after successful bind */
    if (daemon_mode)
        daemonize();

    if (listen(server_fd, BACKLOG) == -1) 
    {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        cleanup();
        return -1;
    }

    /* Timer thread must be started after daemonize() (fork) so it lives in
     * the running daemon process, not the parent that immediately exits. */
    if (pthread_create(&timer_thread, NULL, timer_thread_func, NULL) != 0) 
    {
        syslog(LOG_ERR, "pthread_create (timer): %s", strerror(errno));
        cleanup();
        return -1;
    }
    timer_thread_started = 1;

    while (!caught_signal) 
    {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1)
         {
            if (caught_signal) break;
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            continue;
        }

        struct thread_entry *entry = malloc(sizeof(*entry));
        if (!entry) 
        {
            syslog(LOG_ERR, "malloc failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }
        entry->client_fd = client_fd;
        entry->complete  = 0;

        if (client_addr.ss_family == AF_INET) 
        {
            inet_ntop(AF_INET,
                      &((struct sockaddr_in *)&client_addr)->sin_addr,
                      entry->client_ip, sizeof(entry->client_ip));
        } 
        else 
        {
            inet_ntop(AF_INET6,
                      &((struct sockaddr_in6 *)&client_addr)->sin6_addr,
                      entry->client_ip, sizeof(entry->client_ip));
        }

        syslog(LOG_INFO, "Accepted connection from %s", entry->client_ip);

        if (pthread_create(&entry->tid, NULL, connection_thread_func, entry) != 0)
         {
            syslog(LOG_ERR, "pthread_create: %s", strerror(errno));
            close(client_fd);
            free(entry);
            continue;
        }

        pthread_mutex_lock(&thread_list_lock);
        SLIST_INSERT_HEAD(&thread_list, entry, entries);
        pthread_mutex_unlock(&thread_list_lock);

        reap_completed_threads();
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
    return 0;
}
