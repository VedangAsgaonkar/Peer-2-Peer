#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <dirent.h>
#include <tuple>
#include <algorithm>
using namespace std;

#define MAX_WORDS 50
#define MAXDATASIZE 800

struct client_info
{
    int client_num;
    int port;
};

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// Return a listening socket
int get_listener_socket(const char *PORT)
{
    int listener; // Listening socket descriptor
    int yes = 1;  // For setsockopt() SO_REUSEADDR, below
    int rv;

    struct addrinfo hints, *ai, *p;

    // Get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0)
    {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next)
    {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0)
        {
            continue;
        }

        // Lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0)
        {
            close(listener);
            continue;
        }

        break;
    }

    // If we got here, it means we didn't get bound
    if (p == NULL)
    {
        return -1;
    }

    freeaddrinfo(ai); // All done with this

    // Listen
    if (listen(listener, 10) == -1)
    {
        return -1;
    }

    return listener;
}

// Add a new file descriptor to the set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size)
{
    // If we don't have room, add more space in the pfds array
    if (*fd_count == *fd_size)
    {
        *fd_size *= 2; // Double it

        *pfds = (pollfd *)realloc(*pfds, sizeof(**pfds) * (*fd_size));
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN; // Check ready-to-read

    (*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
{
    // Copy the one from the end over this one
    pfds[i] = pfds[*fd_count - 1];

    (*fd_count)--;
}

int client(int port_num)
{
    char port_str[5];
    sprintf(port_str, "%d", port_num);
    // cout << "setting up client at " << port_num << endl;
    while (1)
    {
        int sockfd;
        struct addrinfo hints, *serverinfo, *p;
        int rv;
        char s[INET6_ADDRSTRLEN];
        bool con_flag = false;

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        if ((rv = getaddrinfo("localhost", port_str, &hints, &serverinfo)) != 0)
        {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        }

        for (p = serverinfo; p != NULL; p = p->ai_next)
        {
            if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
            {
                // fprintf(stderr, "client: socket\n");
                continue;
            }
            if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
            {
                close(sockfd);
                // fprintf(stderr, "client: connect\n");
                continue;
            }
            // cout << "Connected on port " << port_str << endl;
            return sockfd;
        }
    }
    return -1;
}

// Main
int main(int argc, char *argv[])
{
    // ------------------------

    if (argc != 3)
    {
        cout << "Error: Usage -  client1-config.txt files/client1/";
        return 1;
    }

    string file_words[MAX_WORDS];

    fstream config_file;
    config_file.open(argv[1], ios::in);

    int self_client_num = 0;
    config_file >> self_client_num;

    int self_client_port = 0;
    config_file >> self_client_port;

    int self_private_id = 0;
    config_file >> self_private_id;

    int num_neighbours = 0;
    config_file >> num_neighbours;

    struct client_info neighbour_clients[num_neighbours];

    for (int i = 0; i < num_neighbours; i++)
    {
        config_file >> neighbour_clients[i].client_num;
        config_file >> neighbour_clients[i].port;
    }

    int num_files = 0;
    config_file >> num_files;

    string files[num_files];

    for (int i = 0; i < num_files; i++)
    {
        config_file >> files[i];
    }

    // ---------------
    vector<string> self_files;
    DIR *d;
    struct dirent *dir;
    char dir_name[256];
    sprintf(dir_name, "%s", argv[2]);
    d = opendir(dir_name);
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (dir->d_name[0] != '.' && dir->d_type == DT_REG)
            {
                // printf("%s\n", dir->d_name);
                self_files.push_back(dir->d_name);
            }
        }
        closedir(d);
    }

    sort(self_files.begin(), self_files.end());
    for (auto iter = self_files.begin(); iter != self_files.end(); iter++)
    {
        cout << *iter << endl;
    }

    // --------------

    char PORT[5];
    sprintf(PORT, "%d", self_client_port);

    // ------------------------

    int listener; // Listening socket descriptor

    int newfd;                          // Newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // Client address
    socklen_t addrlen;

    char buf[256]; // Buffer for client data

    char remoteIP[INET6_ADDRSTRLEN];

    // Start off with room for 5 connections
    // (We'll realloc as necessary)
    int fd_count = 0;
    int fd_size = 10;
    struct pollfd *pfds = (pollfd *)malloc(sizeof *pfds * fd_size);

    // Set up and get a listening socket
    listener = get_listener_socket(PORT);

    if (listener == -1)
    {
        fprintf(stderr, "error getting listening socket\n");
        exit(1);
    }

    // -----------------------

    int client_sockfd[num_neighbours];
    int server_sockfd[num_neighbours];

    for (int i = 0; i < num_neighbours; i++)
    {
        client_sockfd[i] = client(neighbour_clients[i].port);
    }

    // ------------------------

    // Add the listener to set
    pfds[0].fd = listener;
    pfds[0].events = POLLIN; // Report ready to read on incoming connection

    fd_count = 1; // For the listener

    int num_connections = 0;

    // Main loop
    while (num_connections < num_neighbours)
    {
        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < fd_count; i++)
        {

            // Check if someone's ready to read
            if (pfds[i].revents & POLLIN)
            { // We got one!!

                if (pfds[i].fd == listener)
                {
                    // If listener is ready to read, handle new connection

                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                                   (struct sockaddr *)&remoteaddr,
                                   &addrlen);

                    if (newfd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {
                        add_to_pfds(&pfds, newfd, &fd_count, &fd_size);
                        server_sockfd[num_connections] = newfd;
                        num_connections++;
                    }
                }
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors
    }         // END for(;;)--and you thought it would never end!

    char send_buf[200];
    sprintf(send_buf, "%d,%d,%d", self_client_num, self_private_id, self_client_port);

    for (int i = 0; i < num_neighbours; i++)
    {
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            cerr << "send: error" << endl;
        }
    }

    vector<tuple<int, int, int>> responses;

    for (int i = 0; i < num_neighbours; i++)
    {
        int numbytes;
        char buf[MAXDATASIZE];
        char *token;
        int client_num_int;
        int client_private_id_int;
        int client_port_int;

        if ((numbytes = recv(server_sockfd[i], buf, MAXDATASIZE - 1, 0)) == -1)
        {
            cerr << "recv: error" << endl;
            continue;
        }

        token = strtok(buf, ",");
        client_num_int = stoi(token);
        token = strtok(NULL, ",");
        client_private_id_int = stoi(token);
        token = strtok(NULL, ",");
        client_port_int = stoi(token);
        responses.push_back({client_num_int, client_private_id_int, client_port_int});
    }

    sort(responses.begin(), responses.end());

    for (int i = 0; i < (int)responses.size(); i++)
    {
        printf("Connected to %d with unique-ID %d on port %d\n", get<0>(responses[i]), get<1>(responses[i]), get<2>(responses[i]));
        fflush(stdout);
    }
}