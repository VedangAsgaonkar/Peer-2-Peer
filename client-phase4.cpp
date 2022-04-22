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
#include <map>
#include <unordered_set>
#include <set>
using namespace std;

#define MAX_WORDS 50
#define MAXDATASIZE 201

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

    vector<string> files(num_files);

    for (int i = 0; i < num_files; i++)
    {
        config_file >> files[i];
    }

    sort(files.begin(), files.end());

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
        char in_buf[200];
        char *token;
        int client_num_int;
        int client_private_id_int;
        int client_port_int;

        if ((numbytes = recv(server_sockfd[i], in_buf, MAXDATASIZE - 1, 0)) == -1)
        {
            cerr << "recv: error" << endl;
            continue;
        }

        // cout << numbytes << " bytes recv: " << in_buf << endl;

        // for (int j = 0; j < 800; j++)
        // {
        //     cout << in_buf[j];
        // }

        // cout << endl;

        token = strtok(in_buf, ",");
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

    memset(send_buf, '\0', sizeof send_buf);

    // for (int i = 0; i < num_neighbours; i++)
    // {
    //     memset(send_buf, '\0', sizeof send_buf);
    //     string out_string = "\0";
    //     for (int j = 0; j < num_files; j++)
    //     {
    //         out_string = out_string.append(files[j]);
    //     }
    //     sprintf(send_buf, "%s", out_string.c_str());
    //     if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
    //     {
    //         cout << "send: error" << endl;
    //     }
    //     else
    //     {
    //         // cout << "sent : " << send_buf << endl;
    //     }
    // }

    for (int i = 0; i < num_neighbours; i++)
    {
        for (int j = 0; j < num_files; j++)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "%s", files[j].c_str());
            if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
            {
                cout << "send: error" << endl;
            }
            else
            {
                // cout << "sent : " << send_buf << endl;
            }
        }
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "%s", "$$$");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            cout << "send: error" << endl;
        }
        else
        {
            // cout << "sent : " << send_buf << endl;
        }
    }

    // for (int i = 0; i < num_neighbours; i++)
    // {
    //     int numbytes;
    //     char in_buf[200];

    //     if ((numbytes = recv(server_sockfd[i], in_buf, MAXDATASIZE - 1, 0)) == -1)
    //     {
    //         cout << "recv: error" << endl;
    //         continue;
    //     }

    //     cout << "rcv " << in_buf << endl;
    // }

    struct pollfd pfds2[2 * num_neighbours];

    for (int i = 0; i < 2 * num_neighbours; i++)
    {
        if (i < num_neighbours)
        {
            pfds2[i].fd = server_sockfd[i];
            pfds2[i].events = POLLIN;
        }
        else
        {
            pfds2[i].fd = client_sockfd[i - num_neighbours];
            pfds2[i].events = POLLIN;
        }
    }

    // cout << "Here" << endl;

    std::map<pair<string, int>, bool> responses2;
    int num_connections2 = 0;
    bool term = false;
    unordered_set<int> asked_count1;
    // map<int, int> told_count1;

    while (1)
    {
        if ((num_connections2 >= num_neighbours && term) || num_neighbours == 0)
        {
            break;
        }
        int poll_count = poll(pfds2, 2 * num_neighbours, -1);
        memset(buf, '\0', sizeof buf);
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < 2 * num_neighbours; i++)
        {
            if (asked_count1.find(i) != asked_count1.end())
            {
                continue;
            }

            // Check if someone's ready to read
            if (pfds2[i].revents & POLLIN)
            { // We got one!!
                char in_buf[200];
                int nbytes = recv(pfds2[i].fd, in_buf, sizeof in_buf, 0);
                if (!in_buf)
                {
                    cout << "NULL" << endl;
                    continue;
                }
                // cout << "recv : " << nbytes << " " << in_buf << " from " << i << endl;

                bool found_flag = false;
                if (i < num_neighbours)
                {
                    if (!strcmp(in_buf, "$$$"))
                    {
                        asked_count1.insert(i);
                        continue;
                    }

                    for (int j = 0; j < self_files.size(); j++)
                    {
                        if (!strcmp(in_buf, self_files[j].c_str()))
                        {
                            // cout << in_buf << "Yes" << endl;
                            found_flag = true;
                            memset(send_buf, '\0', sizeof send_buf);
                            sprintf(send_buf, "%s,%d", self_files[j].c_str(), self_private_id);
                            send(pfds2[i].fd, send_buf, sizeof send_buf, 0);
                            memset(send_buf, '\0', sizeof send_buf);
                            break;
                        }
                    }
                    if (!found_flag)
                    {
                        memset(send_buf, '\0', sizeof send_buf);
                        sprintf(send_buf, "%s,%d", string(in_buf).c_str(), -self_private_id);
                        send(pfds2[i].fd, send_buf, sizeof send_buf, 0);
                        memset(send_buf, '\0', sizeof send_buf);
                    }
                }
                else
                {
                    // cout << in_buf << endl;
                    char *token, *recv_file_name;
                    token = strtok(in_buf, ",");
                    if (!strcmp(token, "$$$"))
                    {
                        num_connections2++;
                        continue;
                    }

                    recv_file_name = token;
                    token = strtok(NULL, ",");
                    if (!token)
                    {
                        continue;
                    }
                    int other_private_id_int = stoi(string(token));

                    if (other_private_id_int > 0)
                    {
                        // printf("DEBUG : Found %s at %d with MD5 0 at depth 1\n", recv_file_name, other_private_id_int);
                        // fflush(stdout);
                        responses2.insert({{string(recv_file_name), other_private_id_int}, true});
                    }
                    else
                    {
                        // printf("DEBUG : Not Found %s at %d with MD5 0 at depth 1\n", recv_file_name, -other_private_id_int);
                        // fflush(stdout);
                        responses2.insert({{string(recv_file_name), -other_private_id_int}, false});
                    }
                }
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors

        if (responses2.size() == num_files * num_neighbours && !term)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "$$$");
            for (int j = 0; j < num_neighbours; j++)
            {
                send(server_sockfd[j], send_buf, sizeof send_buf, 0);
            }
            memset(send_buf, '\0', sizeof send_buf);
            term = true;
        }
    }

    std::map<string, int> receipts;
    for (auto iter = responses2.begin(); iter != responses2.end(); iter++)
    {
        // cout << iter->first.first << iter->first.second << iter->second << endl;
        // printf("%s,%d\n", iter->first.first.c_str(), iter->first.second);
        fflush(stdout);
        if (iter->second)
        {
            auto iter2 = receipts.find(iter->first.first);
            if (iter2 == receipts.end())
            {
                receipts.insert({iter->first.first, iter->first.second});
            }
            else
            {
                if (iter2->second > iter->first.second)
                {
                    receipts.erase(iter2);
                    receipts.insert({iter->first.first, iter->first.second});
                }
            }
        }
    }

    // for (auto iter = receipts.begin(); iter != receipts.end(); iter++)
    // {
    //     printf("Found %s at %d with MD5 0 at depth 1\n", iter->first.c_str(), iter->second);
    // }

    // TODO: Check find()

    vector<string> files_not_found_at_depth1;
    for (auto file : files)
    {
        auto check_iter = receipts.find(file);
        if (check_iter == receipts.end())
        {
            // printf("Found %s at %d with MD5 0 at depth 1\n", file.c_str(), 0);
            files_not_found_at_depth1.push_back(file);
        }
        else
        {
            // printf("Found %s at %d with MD5 0 at depth 1\n", check_iter->first.c_str(), check_iter->second);
        }
    }
    fflush(stdout);

    // std::cin.get();

    // ----------------------------

    for (int i = 0; i < num_neighbours; i++)
    {
        for (int j = 0; j < (int)files_not_found_at_depth1.size(); j++)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "%s", files_not_found_at_depth1[j].c_str());
            if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
            {
                cout << "send: error" << endl;
            }
            else
            {
                // cout << "sent : " << send_buf << endl;
            }
        }
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            cout << "send: error" << endl;
        }
        else
        {
            // cout << "sent : " << send_buf << endl;
        }
    }

    // cout << "-------------------------" << endl;

    struct pollfd pfds4[num_neighbours];

    for (int i = 0; i < num_neighbours; i++)
    {
        if (i < num_neighbours)
        {
            pfds4[i].fd = server_sockfd[i];
            pfds4[i].events = POLLIN;
        }
    }

    map<string, vector<int>> asked_fd; // the file descriptors who have asked for a particular file
    unordered_set<int> asked_count;

    for (int i = 0; i < num_neighbours; i++)
    {
        while (true)
        {
            char in_buf[200];
            int nbytes = recv(pfds4[i].fd, in_buf, sizeof in_buf, 0);
            if (nbytes < 0)
            {
                cout << "Silently continue" << endl;
                continue;
            }
            // cout << nbytes << " " << in_buf << endl;
            if (!strcmp(in_buf, "$$$"))
            {
                break;
            }
            auto found = asked_fd.find(string(in_buf));
            if (found == asked_fd.end())
            {
                asked_fd.insert(make_pair(string(in_buf), vector<int>(1, pfds4[i].fd)));
            }
            else
            {
                found->second.push_back(pfds4[i].fd);
            }
        }
    }

    // cout << "-------------------------" << endl;

    for (int i = 0; i < num_neighbours; i++)
    {
        for (auto iter = asked_fd.begin(); iter != asked_fd.end(); iter++)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "%s", iter->first.c_str());
            if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
            {
                cout << "send: error" << endl;
            }
            else
            {
                // cout << "sent : " << send_buf << endl;
            }
        }
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            cout << "send: error" << endl;
        }
        else
        {
            // cout << "sent : " << send_buf << endl;
        }
    }

    // cout << "------------------------" << endl;

    struct pollfd pfds5[2 * num_neighbours];

    for (int i = 0; i < 2 * num_neighbours; i++)
    {
        if (i < num_neighbours)
        {
            pfds5[i].fd = server_sockfd[i];
            pfds5[i].events = POLLIN;
        }
        else
        {
            pfds5[i].fd = client_sockfd[i - num_neighbours];
            pfds5[i].events = POLLIN;
        }
    }

    std::map<pair<string, int>, bool> responses5;
    std::unordered_set<int> asked_count5;
    std::map<int, int> told_count5;
    int num_connections5 = 0;
    term = false;

    while (1)
    {
        if ((num_connections5 >= num_neighbours && term) || num_neighbours == 0)
        {
            break;
        }
        else
        {
            // cout << num_connections5 << num_neighbours << endl;
        }
        int poll_count = poll(pfds5, 2 * num_neighbours, -1);
        memset(buf, '\0', sizeof buf);
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < 2 * num_neighbours; i++)
        {
            if (asked_count5.find(i) != asked_count5.end())
            {
                continue;
            }
            if (told_count5.find(i) != told_count5.end())
            {
                if (told_count5.find(i)->second == (int)asked_fd.size() + 1)
                {
                    // cout << "continuing" << told_count5.find(i)->second << asked_fd.size() << endl;
                    continue;
                }
            }
            // Check if someone's ready to read
            if (pfds5[i].revents & POLLIN)
            { // We got one!!
                char in_buf[200];
                int nbytes = recv(pfds5[i].fd, in_buf, sizeof in_buf, 0);
                // cout << nbytes << " " << in_buf << endl;
                if (!in_buf)
                {
                    cout << "NULL" << endl;
                    continue;
                }

                bool found_flag = false;
                if (i < num_neighbours)
                {
                    // cout << "Server" << i << endl;
                    if (!strcmp(in_buf, "$$$"))
                    {
                        asked_count5.insert(i);
                        continue;
                    }
                    for (int j = 0; j < self_files.size(); j++)
                    {
                        if (!strcmp(in_buf, self_files[j].c_str()))
                        {
                            // cout << in_buf << "Yes" << endl;
                            found_flag = true;
                            memset(send_buf, '\0', sizeof send_buf);
                            sprintf(send_buf, "%s,%d", self_files[j].c_str(), self_private_id);
                            send(pfds5[i].fd, send_buf, sizeof send_buf, 0);
                            memset(send_buf, '\0', sizeof send_buf);
                            break;
                        }
                    }
                    if (!found_flag)
                    {
                        memset(send_buf, '\0', sizeof send_buf);
                        sprintf(send_buf, "%s,%d", string(in_buf).c_str(), -self_private_id);
                        send(pfds5[i].fd, send_buf, sizeof send_buf, 0);
                        memset(send_buf, '\0', sizeof send_buf);
                    }
                }
                else
                {
                    // cout << in_buf << endl;
                    // cout << "Client" << i << endl;
                    char *token, *recv_file_name;
                    token = strtok(in_buf, ",");

                    if (told_count5.find(i) != told_count5.end())
                    {
                        int cnt = told_count5.find(i)->second;
                        cnt++;
                        told_count5.erase(i);
                        told_count5.insert({i, cnt});
                    }
                    else
                    {
                        told_count5.insert({i, 1});
                    }

                    if (!strcmp(token, "$$$"))
                    {
                        num_connections5++;
                        continue;
                    }

                    recv_file_name = token;
                    token = strtok(NULL, ",");
                    if (!token)
                    {
                        continue;
                    }
                    int other_private_id_int = stoi(string(token));

                    if (other_private_id_int > 0)
                    {
                        // printf("DEBUG : Found %s at %d with MD5 0 at depth 1\n", recv_file_name, other_private_id_int);
                        // fflush(stdout);
                        responses5.insert({{string(recv_file_name), other_private_id_int}, true});
                    }
                    else
                    {
                        // printf("DEBUG : Not Found %s at %d with MD5 0 at depth 1\n", recv_file_name, -other_private_id_int);
                        // fflush(stdout);
                        responses5.insert({{string(recv_file_name), -other_private_id_int}, false});
                    }
                }
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors

        if (responses5.size() == asked_fd.size() * num_neighbours && !term)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "$$$");
            for (int j = 0; j < num_neighbours; j++)
            {
                if (send(server_sockfd[j], send_buf, sizeof send_buf, 0) == -1)
                {
                    cout << "error : send" << endl;
                }
                else
                {
                    // cout << "sent: " << send_buf << " to " << server_sockfd[j] << endl;
                }
            }
            memset(send_buf, '\0', sizeof send_buf);
            term = true;
        }
    }

    // cout << "---------------------------------aa" << endl;

    std::map<string, int> receipts5;
    for (auto iter = responses5.begin(); iter != responses5.end(); iter++)
    {
        // cout << iter->first.first << iter->first.second << iter->second << endl;
        // printf("%s,%d\n", iter->first.first.c_str(), iter->first.second);
        fflush(stdout);
        if (iter->second)
        {
            auto iter2 = receipts5.find(iter->first.first);
            if (iter2 == receipts5.end())
            {
                receipts5.insert({iter->first.first, iter->first.second});
            }
            else
            {
                if (iter2->second > iter->first.second)
                {
                    receipts5.erase(iter2);
                    receipts5.insert({iter->first.first, iter->first.second});
                }
            }
        }
    }

    for (auto iter = asked_fd.begin(); iter != asked_fd.end(); iter++)
    {
        auto check_iter = receipts5.find(iter->first);
        if (check_iter == receipts5.end())
        {
            for (int i = 0; i < (int)iter->second.size(); i++)
            {
                // cout << "sockfd " << iter->second[i] << endl;
                memset(send_buf, '\0', sizeof send_buf);
                sprintf(send_buf, "%s,%d", iter->first.c_str(), -1);
                if (send(iter->second[i], send_buf, sizeof send_buf, 0) == -1)
                {
                    cout << "send: error" << endl;
                }
                else
                {
                    // cout << "sent : " << send_buf << endl;
                }
            }
        }
        else
        {
            for (int i = 0; i < (int)iter->second.size(); i++)
            {
                // cout << "sockfd " << iter->second[i] << endl;
                memset(send_buf, '\0', sizeof send_buf);
                sprintf(send_buf, "%s,%d", iter->first.c_str(), check_iter->second);
                if (send(iter->second[i], send_buf, sizeof send_buf, 0) == -1)
                {
                    cout << "send: error" << endl;
                }
                else
                {
                    // cout << "sent : " << send_buf << endl;
                }
            }
        }
    }

    // cout << "---------------------------------bb" << endl;

    for (int i = 0; i < num_neighbours; i++)
    {
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$");
        if (send(server_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            cout << "send: error" << endl;
        }
        else
        {
            // cout << "sent : " << send_buf << endl;
        }
    }

    // cout << "---------------------------------cc" << endl;

    map<string, int> files_at_depth2;

    for (int i = 0; i < num_neighbours; i++)
    {
        while (true)
        {
            char in_buf[200];
            int nbytes = recv(client_sockfd[i], in_buf, sizeof in_buf, 0);
            // cout << "finally : " << nbytes << in_buf << endl;
            if (nbytes < 0)
            {
                break;
            }
            if (!strcmp(in_buf, "$$$"))
            {
                break;
            }
            char *token, *recv_file_name;
            token = strtok(in_buf, ",");
            if (!token)
            {
                continue;
            }
            recv_file_name = token;
            token = strtok(NULL, ",");
            int other_private_id = stoi(token);
            if (other_private_id > 0)
            {
                if (files_at_depth2.find(string(recv_file_name)) != files_at_depth2.end())
                {
                    auto iter = files_at_depth2.find(string(recv_file_name));
                    if (iter->second > other_private_id)
                    {
                        files_at_depth2.erase(iter->first);
                        files_at_depth2.insert({string(recv_file_name), other_private_id});
                    }
                }
                else
                {
                    files_at_depth2.insert({string(recv_file_name), other_private_id});
                }
            }
        }
    }

    for (auto iter = files.begin(); iter != files.end(); iter ++)
    {
        if (receipts.find(*iter) != receipts.end())
        {
            printf("Found %s at %d with MD5 0 at depth 1\n", receipts.find(*iter)->first.c_str(), receipts.find(*iter)->second);
        }
        else if(files_at_depth2.find(*iter) != files_at_depth2.end())
        {
            printf("Found %s at %d with MD5 0 at depth 2\n", files_at_depth2.find(*iter)->first.c_str(), files_at_depth2.find(*iter)->second);
        }
        else
        {
            printf("Found %s at 0 with MD5 0 at depth 0\n", iter->c_str());
        }
    }
}