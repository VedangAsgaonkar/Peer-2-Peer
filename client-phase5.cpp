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
#include <fcntl.h>
#include <openssl/md5.h>
#include <thread>
#include <mutex>
#include <sstream>
#include <errno.h>
#include <sys/wait.h>
#include <filesystem>
#include <cassert>
#include <sys/stat.h>
#include <sys/mman.h>

using namespace std;
namespace fs = std::filesystem;

#define MAX_WORDS 50
#define MAXDATASIZE 201
#define PORT_SIZE 5
#define FILE_BUFFER 200
#define FILE_SIZE_BUFFER 200

struct client_info
{
    int client_num;
    int port;
};

unsigned long get_size_by_fd(int fd)
{
    struct stat statbuf;
    if (fstat(fd, &statbuf) < 0)
        exit(-1);
    return statbuf.st_size;
}

string getstr_md5_sum(unsigned char *md)
{
    int i;
    string result = "";
    for (i = 0; i < MD5_DIGEST_LENGTH; i++)
    {
        char buffer[200];
        sprintf(buffer, "%02x", md[i]);
        string buffer_str = string(buffer);
        result = result + buffer;
    }
    return result;
}

char *getMD5_hash(const string &file_path)
{
    int file_descript;
    unsigned long file_size;
    char *file_buffer;
    unsigned char *result;

    //cout<< "File path for MD_Hash: "<<file_path << endl;
    file_descript = open(file_path.c_str(), O_RDONLY);
    if (file_descript < 0)
    {
        fprintf(stderr, "Unable to open file: %d\n", file_descript);
        return "\0";
    }
    //cout<< "Outside if "<<endl;

    file_size = get_size_by_fd(file_descript);

    //cout<<"Caluclated file size: "<< file_size<<endl;

    result = (unsigned char *)malloc(file_size * sizeof(unsigned char));
    file_buffer = (char *)mmap(0, file_size, PROT_READ, MAP_SHARED, file_descript, 0);
    MD5((unsigned char *)file_buffer, file_size, result);
    munmap(file_buffer, file_size);

    string hash_md5 = getstr_md5_sum(result);
    char *md_result = (char *)malloc((hash_md5.size() + 1) * sizeof(char));
    strcpy(md_result, hash_md5.c_str());

    // return md_result;
    return md_result;
}

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
    // //cout << "setting up client at " << port_num << endl;
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
            // //cout << "Connected on port " << port_str << endl;
            return sockfd;
        }
    }
    return -1;
}

void send_recv_files(int fd, vector<string> file_names, const map<string, int> &file_to_size, string path)
{
    char send_buf[200];
    //cout<<"FD :" <<fd<<endl;
    //cout<<"file_names.size()"<< file_names.size() <<endl;

    auto ret = fs::create_directories(path + "Downloaded");
    if (ret)
    {
        //cout << "created directory tree" << endl;
    }
    else
    {
        //cout << "create_directories() failed" << endl;
    }

    for (string file_name : file_names)
    {

        sprintf(send_buf, "_REQ_:%s", file_name.c_str());
        // cout << send_buf << endl;
        if (send(fd, send_buf, sizeof send_buf, 0) == -1)
        {
            //cout<< "Send error in client"<<endl;
        }

        auto iter_check = file_to_size.find(file_name);
        int file_size = 0;
        if (iter_check != file_to_size.end())
        {
            file_size = iter_check->second;
        }

        //cout<<"File size to be received:" <<file_size << endl;
        char text[FILE_BUFFER];
        FILE *download_file = fopen((path + "Downloaded/" + file_name).c_str(), "wb");
        int received_size = 0;
        // cout << "Receiving file: " << file_name << endl;
        while (received_size < file_size)
        {
            int datasize = recv(fd, text, sizeof(text), 0);
            if (datasize < 0)
            {
                continue;
            }
            fwrite(&text, 1, datasize, download_file);
            received_size += datasize;
            //cout<<"Current status: "<< received_size << endl;

            char thread_send_buf[200];
            memset(thread_send_buf, '\0', sizeof thread_send_buf);
            sprintf(thread_send_buf, "%s", "_ACK_");
            send(fd, thread_send_buf, sizeof thread_send_buf, 0);
        }
        fclose(download_file);
        // cout << "File received" << endl;
    }
    return;
}

void send_recv_files_depth2(int fd, vector<string> file_names, const map<string, int> &file_to_size, string path)
{
    char send_buf[200];
    //cout<<"FD :" <<fd<<endl;
    //cout<<"file_names.size()"<< file_names.size() <<endl;

    auto ret = fs::create_directories(path + "Downloaded");
    if (ret)
    {
        //cout << "created directory tree" << endl;
    }
    else
    {
        //cout << "create_directories() failed" << endl;
    }

    for (string file_name : file_names)
    {

        sprintf(send_buf, "_REQ_:%s", file_name.c_str());
        // cout << "sent :" << send_buf << " fd: " << fd  << endl;
        if (send(fd, send_buf, sizeof send_buf, 0) == -1)
        {
            cout << "Send error in client" << endl;
        }

        auto iter_check = file_to_size.find(file_name);
        int file_size = 0;
        if (iter_check != file_to_size.end())
        {
            file_size = iter_check->second;
        }

        // cout<<"File size to be receiving:" <<file_size << endl;
        char text[FILE_BUFFER];
        FILE *download_file = fopen((path + "Downloaded/" + file_name).c_str(), "wb");
        int received_size = 0;
        // cout<<"Receiving file: "<< file_name << endl;
        int i = 0;
        while (received_size < file_size)
        {
            int datasize = recv(fd, text, sizeof(text), 0);
            if (datasize < 0)
            {
                continue;
            }

            if (datasize == 0)
            {
                break;
            }

            fwrite(&text, 1, datasize, download_file);
            received_size += datasize;
            // cout << "Current status: " << received_size << endl;

            char thread_send_buf[200];
            memset(thread_send_buf, '\0', sizeof thread_send_buf);
            sprintf(thread_send_buf, "%s", "_ACK_");
            send(fd, thread_send_buf, sizeof thread_send_buf, 0);
        }
        fclose(download_file);
        //cout<<"File received" << endl;
    }
    return;
}

// Main
int main(int argc, char *argv[])
{
    // ------------------------

    if (argc != 3)
    {
        //cout << "Error: Usage -  client1-config.txt files/client1/";
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
                // //printf("%s\n", dir->d_name);
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
    map<int, int> allid_to_fd;
    map<int, int> port_to_fd;
    for (int i = 0; i < num_neighbours; i++)
    {
        client_sockfd[i] = client(neighbour_clients[i].port);
        port_to_fd.insert({neighbour_clients[i].port, client_sockfd[i]});
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
            cerr << "send: error" << __LINE__ << endl;
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

        // //cout << numbytes << " bytes recv: " << in_buf << endl;

        // for (int j = 0; j < 800; j++)
        // {
        //     //cout << in_buf[j];
        // }

        // //cout << endl;

        token = strtok(in_buf, ",");
        client_num_int = stoi(token);
        token = strtok(NULL, ",");
        client_private_id_int = stoi(token);
        token = strtok(NULL, ",");
        client_port_int = stoi(token);
        responses.push_back({client_num_int, client_private_id_int, client_port_int});
        allid_to_fd.insert({client_private_id_int, port_to_fd.find(client_port_int)->second});
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
    //         //cout << "send: error" << __LINE__ << endl;
    //     }
    //     else
    //     {
    //         // //cout << "sent : " << send_buf << endl;
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
                //cout << "send: error" << __LINE__ << endl;
            }
            else
            {
                // //cout << "sent : " << send_buf << endl;
            }
        }
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "%s", "$$$");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            //cout << "send: error" << __LINE__ << endl;
        }
        else
        {
            // //cout << "sent : " << send_buf << endl;
        }
    }

    // for (int i = 0; i < num_neighbours; i++)
    // {
    //     int numbytes;
    //     char in_buf[200];

    //     if ((numbytes = recv(server_sockfd[i], in_buf, MAXDATASIZE - 1, 0)) == -1)
    //     {
    //         //cout << "recv: error" << endl;
    //         continue;
    //     }

    //     //cout << "rcv " << in_buf << endl;
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

    // //cout << "Here" << endl;

    std::map<pair<string, int>, bool> responses2;
    std::map<string, int> file_to_size_depth1;
    std::map<int, int> clientid_to_fd_depth1;

    int num_connections2 = 0;
    bool term = false;
    unordered_set<int> asked_count1;
    int expected_req = 0;

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
                    //cout << "NULL" << endl;
                    continue;
                }
                // //cout << "recv : " << nbytes << " " << in_buf << " from " << i << endl;

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
                            // //cout << in_buf << "Yes" << endl;
                            found_flag = true;
                            memset(send_buf, '\0', sizeof send_buf);
                            std::filesystem::path fd_path{(string(argv[2]) + self_files[j]).c_str()};
                            int size_of_file = static_cast<int>(std::filesystem::file_size(fd_path));

                            sprintf(send_buf, "%s,%d,%d", self_files[j].c_str(), self_private_id, size_of_file);
                            send(pfds2[i].fd, send_buf, sizeof send_buf, 0);
                            // cout << "Sending" << send_buf << endl;
                            memset(send_buf, '\0', sizeof send_buf);

                            expected_req++;
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
                    // //cout << in_buf << endl;
                    char *token, *recv_file_name;
                    int recv_file_size = -1;
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
                        token = strtok(NULL, ",");
                        if (!token)
                        {
                            continue;
                        }

                        recv_file_size = stoi(token);
                        // //printf("DEBUG : Found %s at %d with MD5 0 at depth 1\n", recv_file_name, other_private_id_int);
                        // fflush(stdout);
                        auto iter_check = file_to_size_depth1.find(string(recv_file_name));
                        // TODO: Consider different file sizes for same name with different clients
                        clientid_to_fd_depth1.insert({other_private_id_int, client_sockfd[i - num_neighbours]});
                        if (iter_check == file_to_size_depth1.end())
                        {
                            file_to_size_depth1.insert({string(recv_file_name), recv_file_size});
                        }
                        responses2.insert({{string(recv_file_name), other_private_id_int}, true});
                    }
                    else
                    {
                        // //printf("DEBUG : Not Found %s at %d with MD5 0 at depth 1\n", recv_file_name, -other_private_id_int);
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

    // cout << "----------------------F-3" << endl;

    std::map<string, int> receipts;
    std::map<string, vector<int>> rejections_depth1;

    for (auto iter = responses2.begin(); iter != responses2.end(); iter++)
    {
        // //cout << iter->first.first << iter->first.second << iter->second << endl;
        // //printf("%s,%d\n", iter->first.first.c_str(), iter->first.second);
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
                    auto rej_check_iter = rejections_depth1.find(iter->first.first);
                    if (rej_check_iter == rejections_depth1.end())
                    {
                        auto iter_check = clientid_to_fd_depth1.find(iter2->second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd_depth1.end());
                        int fd = iter_check->second;
                        vector<int> temp = {fd};
                        rejections_depth1.insert({iter->first.first, temp});
                    }
                    else
                    {
                        auto iter_check = clientid_to_fd_depth1.find(iter2->second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd_depth1.end());
                        int fd = iter_check->second;
                        rej_check_iter->second.push_back(fd);
                    }
                    receipts.erase(iter2);
                    receipts.insert({iter->first.first, iter->first.second});
                }
                else
                {
                    auto rej_check_iter = rejections_depth1.find(iter->first.first);
                    if (rej_check_iter == rejections_depth1.end())
                    {
                        auto iter_check = clientid_to_fd_depth1.find(iter->first.second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd_depth1.end());
                        int fd = iter_check->second;
                        vector<int> temp = {fd};
                        rejections_depth1.insert({iter->first.first, temp});
                    }
                    else
                    {
                        auto iter_check = clientid_to_fd_depth1.find(iter->first.second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd_depth1.end());
                        int fd = iter_check->second;
                        rej_check_iter->second.push_back(fd);
                    }
                }
            }
        }
    }

    // Inverting receipts
    std::map<int, vector<string>> id_to_files;

    for (auto iter = receipts.begin(); iter != receipts.end(); iter++)
    {
        auto iter_check = id_to_files.find(iter->second);
        if (iter_check == id_to_files.end())
        {
            vector<string> temp_vec = {iter->first};
            id_to_files.insert({iter->second, temp_vec});
        }
        else
        {
            iter_check->second.push_back(iter->first);
        }
    }

    std::thread file_transfer_threads_depth1[static_cast<int>(id_to_files.size())];
    // spawning threads to get the required files
    int thread_num_depth1 = 0;
    for (auto iter = id_to_files.begin(); iter != id_to_files.end(); iter++)
    {
        auto iter_check = clientid_to_fd_depth1.find(iter->first);
        // TODO : remove assert
        assert(iter_check != clientid_to_fd_depth1.end());
        // cout << "Spawned thread for client private id :" << iter->first << ":" << iter_check->second << endl;

        int fd = iter_check->second;

        file_transfer_threads_depth1[thread_num_depth1] = std::thread(send_recv_files, iter_check->second, iter->second, file_to_size_depth1, string(argv[2]));
        thread_num_depth1++;
    }

    for (auto elem : rejections_depth1)
    {
        for (auto fd : elem.second)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "_REJ_");
            send(fd, send_buf, sizeof send_buf, 0);
        }
    }

    // for (int i = 0; i < num_neighbours; i++)
    // {
    //     memset(send_buf, '\0', sizeof send_buf);
    //     sprintf(send_buf, "$$$");
    //     if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
    //     {
    //         //cout << "send: error" << __LINE__ << endl;
    //     }
    //     else
    //     {
    //         // //cout << "sent : " << send_buf << endl;
    //     }
    // }

    struct pollfd pfds3[2 * num_neighbours];
    // server sending the requested files
    for (int i = 0; i < 2 * num_neighbours; i++)
    {
        if (i < num_neighbours)
        {
            pfds3[i].fd = server_sockfd[i];
            pfds3[i].events = POLLIN;
        }
        else
        {
            pfds3[i].fd = client_sockfd[i - num_neighbours];
            pfds3[i].events = POLLIN;
        }
    }

    // cout << "Server: expected req/rej: " << expected_req << endl;
    int count_req = 0;
    // unordered_set<int> asked_rej;
    while (count_req < expected_req)
    {

        int poll_count = poll(pfds3, num_neighbours, -1);
        memset(buf, '\0', sizeof buf);
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < num_neighbours; i++)
        {
            // if(asked_rej.find(i) != asked_rej.end())
            // {
            //     continue;
            // }
            // Check if someone's ready to read
            if (pfds3[i].revents & POLLIN)
            { // We got one!!
                ////cout<< "Inside if"<<endl;
                char in_buf[200];
                int nbytes = recv(pfds3[i].fd, in_buf, sizeof in_buf, 0);
                if (!in_buf)
                {
                    //cout << "NULL" << endl;
                    continue;
                }
                if (nbytes == 0)
                {
                    continue;
                }
                // cout << "recv : " << in_buf << " from " << pfds3[i].fd << endl;

                // if(!strcmp(in_buf, "$$$"))
                // {
                //     asked_rej.insert(i);
                //     continue;
                // }

                string in_buf_str = in_buf;

                string type = in_buf_str.substr(0, 5);
                if (type.compare("_REQ_") != 0)
                {
                    count_req++;
                    continue;
                }
                else
                {
                    count_req++;
                }
                string file_req_str = in_buf + 6 * sizeof(char);
                //cout<<"Requested file: "<< file_req_str << endl;

                // char file_size_send[FILE_SIZE_BUFFER];
                // auto iter_file_size = file_to_size.find(file_req_str);

                // if(iter_file_size != file_to_size.end())
                // {
                //     sprintf(file_size_send, "%d:", iter_file_size->second);
                // }

                // send(pfds2[i].fd, file_size_send, sizeof file_size_send, 0);

                FILE *req_file = fopen((string(argv[2]) + file_req_str).c_str(), "rb");
                int bytes_read;
                char chunk_buffer[FILE_BUFFER];
                //cout<<"Sending file: "<<file_req_str<<endl;
                while (!feof(req_file))
                {
                    if ((bytes_read = fread(&chunk_buffer, 1, FILE_BUFFER, req_file)) > 0)
                    {
                        if (send(pfds3[i].fd, chunk_buffer, bytes_read, 0) == -1)
                        {
                            //cout<<"Error sending"<<endl;
                        }
                    }
                    else
                    {
                        //cout<<"Byte read:"<<bytes_read<< endl;
                        break;
                    }

                    char in_buf[200];
                    int numbytes = recv(pfds3[i].fd, in_buf, sizeof in_buf, 0);
                    // cout << "in_buf: " << in_buf<< endl;
                    if (!strcmp(in_buf, "_ACK_"))
                    {
                        continue;
                    }
                    else
                    {
                        // cout << "NO ACK" << endl;
                        break;
                    }
                }

                fclose(req_file);
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors
    }

    for (int i = 0; i < static_cast<int>(id_to_files.size()); i++)
    {
        file_transfer_threads_depth1[i].join();
    }

    // cout << "Depth1 file sharing completed" << endl;

    sleep(5);

    // TODO: Check find()

    vector<string> files_not_found_at_depth1;
    for (auto file : files)
    {
        auto check_iter = receipts.find(file);
        if (check_iter == receipts.end())
        {
            //printf("Found %s at %d with MD5 0 at depth 1\n", file.c_str(), 0);
            files_not_found_at_depth1.push_back(file);
            //cout << "File to be requested for depth 2: " << file << endl;
        }
        else
        {
            //printf("Found %s at %d with MD5 0 at depth 0\n", check_iter->first.c_str(), check_iter->second);
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
                //cout << "send: error" << __LINE__ << endl;
            }
            else
            {
                // //cout << "sent : " << send_buf << endl;
            }
        }
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            //cout << "send: error" << __LINE__ << endl;
        }
        else
        {
            // //cout << "sent : " << send_buf << endl;
        }
    }

    // //cout << "-------------------------" << endl;

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
                //cout << "Silently continue" << endl;
                continue;
            }
            // //cout << nbytes << " " << in_buf << endl;
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

    // //cout << "-------------------------" << endl;

    for (int i = 0; i < num_neighbours; i++)
    {
        for (auto iter = asked_fd.begin(); iter != asked_fd.end(); iter++)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "%s", iter->first.c_str());
            if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
            {
                //cout << "send: error" << __LINE__ << endl;
            }
            else
            {
                // //cout << "sent : " << send_buf << endl;
            }
        }
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            //cout << "send: error" << __LINE__ << endl;
        }
        else
        {
            // //cout << "sent : " << send_buf << endl;
        }
    }

    // //cout << "------------------------" << endl;

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

    // cout << "--------------------------------F-2" << endl;

    std::map<pair<string, int>, bool> responses5;
    std::unordered_set<int> asked_count5;
    std::map<int, int> told_count5;
    std::map<int, int> clientid_to_fd_depth2;
    int num_connections5 = 0;
    term = false;
    int num_req_depth2 = 0;
    map<string, int> file_to_size_depth2;
    while (1)
    {
        if ((num_connections5 >= num_neighbours && term) || num_neighbours == 0)
        {
            break;
        }
        else
        {
            // //cout << num_connections5 << num_neighbours << endl;
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
                    // //cout << "continuing" << told_count5.find(i)->second << asked_fd.size() << endl;
                    continue;
                }
            }
            // Check if someone's ready to read
            if (pfds5[i].revents & POLLIN)
            { // We got one!!
                char in_buf[200];
                int nbytes = recv(pfds5[i].fd, in_buf, sizeof in_buf, 0);
                // //cout << nbytes << " " << in_buf << endl;
                if (!in_buf)
                {
                    //cout << "NULL" << endl;
                    continue;
                }

                bool found_flag = false;
                if (i < num_neighbours)
                {
                    // //cout << "Server" << i << endl;
                    if (!strcmp(in_buf, "$$$"))
                    {
                        //cout<< "Received $$$ for depth2 checking" << endl;
                        asked_count5.insert(i);
                        continue;
                    }
                    for (int j = 0; j < self_files.size(); j++)
                    {
                        if (!strcmp(in_buf, self_files[j].c_str()))
                        {
                            // //cout << in_buf << "Yes" << endl;
                            found_flag = true;
                            memset(send_buf, '\0', sizeof send_buf);
                            std::filesystem::path fd_path{(string(argv[2]) + self_files[j]).c_str()};
                            int size_of_file = static_cast<int>(std::filesystem::file_size(fd_path));

                            sprintf(send_buf, "%s,%d,%d", self_files[j].c_str(), self_private_id, size_of_file);
                            //cout << "File size found: " << size_of_file <<"," << self_files[j] <<endl;
                            send(pfds5[i].fd, send_buf, sizeof send_buf, 0);
                            memset(send_buf, '\0', sizeof send_buf);
                            num_req_depth2++;

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
                    // //cout << in_buf << endl;
                    // //cout << "Client" << i << endl;
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
                        token = strtok(NULL, ",");
                        if (!token)
                        {
                            continue;
                        }

                        int recv_file_size = stoi(token);
                        //cout << "First travel : " << recv_file_name << "," << recv_file_size << endl;

                        clientid_to_fd_depth2.insert({other_private_id_int, client_sockfd[i - num_neighbours]});
                        auto iter_check = file_to_size_depth2.find(string(recv_file_name));
                        // TODO: Consider different file sizes for same name with different clients
                        if (iter_check == file_to_size_depth2.end())
                        {
                            //cout << "inserting"<<endl;
                            file_to_size_depth2.insert({string(recv_file_name), recv_file_size});
                        }
                        // //printf("DEBUG : Found %s at %d with MD5 0 at depth 1\n", recv_file_name, other_private_id_int);
                        // fflush(stdout);
                        responses5.insert({{string(recv_file_name), other_private_id_int}, true});
                    }
                    else
                    {
                        // //printf("DEBUG : Not Found %s at %d with MD5 0 at depth 1\n", recv_file_name, -other_private_id_int);
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
                    //cout << "error : send" << endl;
                }
                else
                {
                    // //cout << "sent: " << send_buf << " to " << server_sockfd[j] << endl;
                }
            }
            memset(send_buf, '\0', sizeof send_buf);
            term = true;
        }
    }

    //cout << "Depth2 checking completed" << endl;
    // //cout << "---------------------------------aa" << endl;

    std::map<string, int> receipts5;
    std::map<string, vector<int>> rejections5;
    for (auto iter = responses5.begin(); iter != responses5.end(); iter++)
    {
        // //cout << iter->first.first << iter->first.second << iter->second << endl;
        // //printf("%s,%d\n", iter->first.first.c_str(), iter->first.second);
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
                    auto rej_check_iter = rejections5.find(iter->first.first);
                    if (rej_check_iter == rejections5.end())
                    {
                        auto iter_check = clientid_to_fd_depth2.find(iter2->second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd_depth2.end());
                        int fd = iter_check->second;
                        vector<int> temp = {fd};
                        rejections5.insert({iter->first.first, temp});
                    }
                    else
                    {
                        auto iter_check = clientid_to_fd_depth2.find(iter2->second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd_depth2.end());
                        int fd = iter_check->second;
                        rej_check_iter->second.push_back(fd);
                    }
                    receipts5.erase(iter2);
                    receipts5.insert({iter->first.first, iter->first.second});
                }
            }
        }
    }
    //cout << "Printing file to size: "<< endl;
    // cout << "-----------------------------F-1" << endl;
    for (auto iter = file_to_size_depth2.begin(); iter != file_to_size_depth2.end(); iter++)
    {
        //cout<<iter ->first << "," << iter -> second<<endl;
    }
    int num_req_depth2_x = 0; // for B -> A
    vector<int> total_sent_neigh;
    for (auto iter = asked_fd.begin(); iter != asked_fd.end(); iter++)
    {
        auto check_iter = receipts5.find(iter->first);
        if (check_iter == receipts5.end())
        {
            for (int i = 0; i < (int)iter->second.size(); i++)
            {
                // //cout << "sockfd " << iter->second[i] << endl;
                memset(send_buf, '\0', sizeof send_buf);
                sprintf(send_buf, "%s,%d", iter->first.c_str(), -1);
                if (send(iter->second[i], send_buf, sizeof send_buf, 0) == -1)
                {
                    //cout << "send: error" << __LINE__ << endl;
                }
                else
                {
                    // //cout << "sent : " << send_buf << endl;
                }
            }
        }
        else
        {
            for (int i = 0; i < (int)iter->second.size(); i++)
            {
                // //cout << "sockfd " << iter->second[i] << endl;
                memset(send_buf, '\0', sizeof send_buf);
                int port_num_depth2 = -1;
                for (auto res_iter = responses.begin(); res_iter != responses.end(); res_iter++)
                {
                    if (get<1>(*res_iter) == check_iter->second)
                    {
                        port_num_depth2 = get<2>(*res_iter);
                    }
                }
                sprintf(send_buf, "%s,%d,%d,%d", iter->first.c_str(), check_iter->second, file_to_size_depth2.find(iter->first.c_str())->second, port_num_depth2);
                num_req_depth2_x++;
                if (find(total_sent_neigh.begin(), total_sent_neigh.end(), iter->second[i]) == total_sent_neigh.end())
                {
                    total_sent_neigh.push_back(iter->second[i]);
                }

                //cout << "Current number of req sent at depth2 for B :" << num_req_depth2_x <<" B:"<<send_buf <<endl;
                if (send(iter->second[i], send_buf, sizeof send_buf, 0) == -1)
                {
                    //cout << "send: error" << __LINE__ << endl;
                }
                else
                {
                    // //cout << "sent : " << send_buf << endl;
                }
            }
        }
    }

    //cout << "Outside ask_fd for" <<endl;

    // //cout << "---------------------------------bb" << endl;

    for (int i = 0; i < num_neighbours; i++)
    {
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$");
        if (send(server_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            //cout << "send: error" << __LINE__ << endl;
        }
        else
        {
            // //cout << "sent : " << send_buf << endl;
        }
    }

    // //cout << "---------------------------------cc" << endl;

    map<string, vector<pair<int, int>>> files_at_depth2;
    map<string, int> file_to_size_depth2_client;
    // stores depth 2 client id to depth 2 port
    map<int, int> clientid_to_port;

    for (int i = 0; i < num_neighbours; i++)
    {
        while (true)
        {
            char in_buf[200];
            int nbytes = recv(client_sockfd[i], in_buf, sizeof in_buf, 0);
            // //cout << "finally : " << nbytes << in_buf << endl;
            int recv_file_size = -1;
            int recv_port_num = -1;
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

            //cout <<"File name received: " <<recv_file_name << endl;
            token = strtok(NULL, ",");
            int other_private_id = stoi(token);
            if (other_private_id > 0)
            {
                token = strtok(NULL, ",");
                recv_file_size = stoi(token);
                //cout <<"File name size: " <<recv_file_size << endl;

                token = strtok(NULL, ",");
                recv_port_num = stoi(token);
                //cout <<"File port: " <<recv_port_num << endl;
                if (file_to_size_depth2_client.find(string(recv_file_name)) == file_to_size_depth2_client.end())
                {
                    file_to_size_depth2_client.insert({string(recv_file_name), recv_file_size});
                }

                if (clientid_to_port.find(other_private_id) == clientid_to_port.end())
                {
                    clientid_to_port.insert({other_private_id, recv_port_num});
                }

                // {
                //     file_to_size_depth2_client.insert({string(recv_file_name), recv_file_size});
                // }

                auto iter_check_files = files_at_depth2.find(string(recv_file_name));
                if (iter_check_files == files_at_depth2.end())
                {
                    // file_at_depth2 : filename, <depth2 id, depth 1 fd>;
                    files_at_depth2.insert({string(recv_file_name), vector<pair<int, int>>(1, {other_private_id, client_sockfd[i]})});
                }
                else
                {
                    iter_check_files->second.push_back({other_private_id, client_sockfd[i]});
                }
            }
            else
            {
                //files_at_depth2.insert({string(recv_file_name), 0});
            }
        }
    }

    // cout << "--------------------------------F0" << endl;

    //cout << "printed" << endl;
    //sending rej/$$$/acc to neighbour
    vector<int> fd_dollar;
    vector<int> dollar_sent;
    for (auto elem : files_at_depth2)
    {
        // cout << "Files at depth 2 : " << elem.first << endl;
        sort(elem.second.begin(), elem.second.end());
        for (int i = 0; i < elem.second.size(); i++)
        {
            // cout << elem.second[i].first << "," << elem.second[i].second << endl;
            if (i == 0)
            {
                // cout << "acc"<< endl;
                memset(send_buf, '\0', sizeof send_buf); //CHECK BELOW
                sprintf(send_buf, "_ACC_,%s,%d", elem.first.c_str(), self_private_id);
                // cout<<"abc: " << send_buf << endl;
                fd_dollar.push_back(elem.second[i].second);
                // //cout << ""
                if (send(elem.second[i].second, send_buf, sizeof send_buf, 0) == -1)
                {
                    //cout << "send: error" << __LINE__ << endl;
                }
            }
            else
            {
                // cout << "rej"<< endl;
                memset(send_buf, '\0', sizeof send_buf);
                sprintf(send_buf, "_REJ_,%s", elem.first.c_str());
                // cout<<"abc: " << send_buf << endl;
                fd_dollar.push_back(elem.second[i].second);
                if (send(elem.second[i].second, send_buf, sizeof send_buf, 0) == -1)
                {
                    //cout << "send: error" << __LINE__ << endl;
                }
                else
                {
                    // //cout << "sent : " << send_buf << endl;
                }
            }
        }
    }
    // invert files_at_depth2
    map<int, vector<string>> clientid_to_files;

    for (auto iter = files_at_depth2.begin(); iter != files_at_depth2.end(); iter++)
    {
        auto iter_check = clientid_to_files.find(iter->second[0].first);
        if (iter_check == clientid_to_files.end())
        {
            vector<string> temp_vec = {iter->first};
            clientid_to_files.insert({iter->second[0].first, temp_vec});
        }
        else
        {
            iter_check->second.push_back(iter->first);
        }
    }

    // cout << "fd_size " << fd_dollar.size()<<endl;
    // sending $$$
    for (int s : fd_dollar)
    {
        auto iter_check = find(dollar_sent.begin(), dollar_sent.end(), s);
        if (iter_check == dollar_sent.end())
        {
            dollar_sent.push_back(s);
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "###");
            if (send(s, send_buf, sizeof send_buf, 0) == -1)
            {
                cout << "send: error" << __LINE__ << endl;
            }
        }
    }

    // cout << "------------------------------F1" << endl;
    sleep(1);
    // cin.get();

    // responding to ACC/$$$/REJ
    int current_req_x = 0;
    //map from file name to client id
    vector<pair<string, int>> accepted_req;
    vector<string> rej_files_x;
    vector<int> recv_rej;
    // cout << "num_req_depth2_x : " << num_req_depth2_x << endl;
    // cout << "total sent: " << total_sent_neigh.size() << endl;

    while (1)
    {
        if (current_req_x >= num_req_depth2_x && recv_rej.size() == total_sent_neigh.size())
        {
            break;
        }
        int poll_count = poll(pfds2, num_neighbours, -1);
        memset(buf, '\0', sizeof buf);
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read

        for (int i = 0; i < num_neighbours; i++)
        {
            // Check if someone's ready to read
            if (pfds2[i].revents & POLLIN)
            { // We got one!!
                if (find(recv_rej.begin(), recv_rej.end(), i) != recv_rej.end())
                {
                    continue;
                }
                char in_buf[200];
                int nbytes = recv(pfds2[i].fd, in_buf, sizeof in_buf, 0);
                // cout << "buggy: "  << nbytes << " " << in_buf << endl;
                if (!in_buf)
                {
                    //cout << "NULL" << endl;
                    continue;
                }

                if (!strcmp(in_buf, string("###").c_str()))
                {
                    recv_rej.push_back(i);
                    continue;
                }
                char *token;
                string file_name_rej;
                int depth2_client;
                token = strtok(in_buf, ",");
                if (!strcmp(token, string("_ACC_").c_str()))
                {
                    token = strtok(NULL, ",");
                    file_name_rej = string(token);
                    token = strtok(NULL, ",");

                    depth2_client = stoi(token);
                    accepted_req.push_back({file_name_rej, depth2_client});
                    current_req_x++;

                    continue;
                }
                if (!strcmp(token, string("_REJ_").c_str()))
                {
                    token = strtok(NULL, ",");
                    file_name_rej = string(token);
                    rej_files_x.push_back(file_name_rej);
                    current_req_x++;
                }
                // //cout << "recv : " << nbytes << " " << in_buf << " from " << i << endl;
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors
    }

    // cout << "------------------------------F2" << endl;
    sleep(1);

    //cout << "-------------------------------------------" << endl;

    //updating the rejection list and receipts list
    // for(auto elem : rej_files_x)
    // {
    //     auto found_iter = receipts5.find(elem);
    //     if(found_iter != receipts5.end())
    //     {
    //         receipts5.erase(found_iter);
    //         if(rejections5.find(found_iter->first) != rejections5.end())
    //         {
    //             rejections5.find(found_iter->first)->second.push_back(found_iter->second);
    //         }
    //         else
    //         {
    //             rejections5.insert({found_iter->first, vector<int>(1, found_iter->second)});
    //         }
    //     }
    // }
    // sending acc anD $$$
    for (auto elem : accepted_req)
    {
        int id_tmp = elem.second;
        int fd = allid_to_fd.find(receipts5.find(elem.first)->second)->second;
        memset(send_buf, '\0', sizeof send_buf); //CHECK BELOW
        sprintf(send_buf, "_ACC_,%d", id_tmp);
        // cout << "htg"<<send_buf<<endl;
        if (send(fd, send_buf, sizeof send_buf, 0) == -1)
        {
            //cout << "send: error" << __LINE__ << endl;
        }
    }
    for (int i = 0; i < num_neighbours; i++)
    {
        memset(send_buf, '\0', sizeof send_buf);
        sprintf(send_buf, "$$$1");
        if (send(client_sockfd[i], send_buf, sizeof send_buf, 0) == -1)
        {
            //cout << "send: error" << __LINE__ << endl;
        }
    }

    // cout << "------------------------------F3" << endl;
    sleep(1);

    int dollar_count = 0;
    int num_depth2_req = 0;
    vector<int> neigh_ids_depth2;
    unordered_set<int> asked_count6;
    while (dollar_count < num_neighbours)
    {
        int poll_count = poll(pfds5, num_neighbours, -1);
        memset(buf, '\0', sizeof buf);
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < num_neighbours; i++)
        {
            if (asked_count6.find(i) != asked_count6.end())
            {
                continue;
            }
            // Check if someone's ready to read
            if (pfds5[i].revents & POLLIN)
            { // We got one!!
                char in_buf[200];
                int nbytes = recv(pfds5[i].fd, in_buf, sizeof in_buf, 0);
                // cout << __LINE__ << "recv: " << nbytes << " " << in_buf << endl;
                if (!in_buf)
                {
                    //cout << "NULL" << endl;
                    continue;
                }
                if (!strcmp(in_buf, string("$$$1").c_str()))
                {
                    dollar_count++;
                    asked_count6.insert(i);
                    continue;
                }
                char *token;
                int id_tmp;
                token = strtok(in_buf, ",");
                if (!strcmp(token, string("_ACC_").c_str()))
                {
                    token = strtok(NULL, ",");
                    id_tmp = stoi(token);
                    num_depth2_req++;
                    if (find(neigh_ids_depth2.begin(), neigh_ids_depth2.end(), id_tmp) == neigh_ids_depth2.end())
                    {
                        neigh_ids_depth2.push_back(id_tmp);
                        // cout<<"Pushing id " << id_tmp <<endl ;
                    }
                }
            }
        }
    }

    // cout << "------------------------------F4" << endl;
    sleep(1);

    int num_depth2_neigh = static_cast<int>(neigh_ids_depth2.size());
    // cout <<  "Num of depth 2 ngb" << num_depth2_neigh << endl;

    // connections and file transfer
    int depth_connections[static_cast<int>(clientid_to_files.size())];
    int depth_connections_cnt = 0;
    map<int, int> id_to_fd_ult;
    for (auto elem : clientid_to_files)
    {
        depth_connections[depth_connections_cnt] = client(clientid_to_port.find(elem.first)->second);
        // cout << "Connecting to client id " << elem.first << " on fd : " << depth_connections[depth_connections_cnt] << endl;
        //cout << "port "<<clientid_to_port.find(elem.first)->second;
        id_to_fd_ult.insert({elem.first, depth_connections[depth_connections_cnt]});
        depth_connections_cnt++;
    }

    int curr_connect = 1;
    int server_sockfd_depth2[num_depth2_neigh];
    int num_connections_depth2 = 0;
    int fd_size_depth2 = num_depth2_neigh;
    struct pollfd *pfds_x = (pollfd *)malloc(sizeof *pfds_x * fd_size_depth2);
    pfds_x[0].fd = listener;
    pfds_x[0].events = POLLIN;

    //
    while (num_connections_depth2 < num_depth2_neigh)
    {
        int poll_count = poll(pfds_x, curr_connect, -1);

        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < curr_connect; i++)
        {

            // Check if someone's ready to read
            if (pfds_x[i].revents & POLLIN)
            { // We got one!!

                if (pfds_x[i].fd == listener)
                {
                    // If listener is ready to read, handle new connection

                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                                   (struct sockaddr *)&remoteaddr,
                                   &addrlen);

                    // cout << "Accepted" << newfd << endl;

                    if (newfd == -1)
                    {
                        perror("accept");
                    }
                    else
                    {
                        add_to_pfds(&pfds_x, newfd, &curr_connect, &fd_size_depth2);
                        server_sockfd_depth2[num_connections_depth2] = newfd;
                        num_connections_depth2++;
                    }
                }
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors
    }
    // END for(;;)--and you thought it would never end!

    // cout << "------------------------------F5" << endl;
    sleep(1);

    std::thread file_transfer_threads_depth2[static_cast<int>(clientid_to_files.size())];
    // spawning threads to get the required files
    // int fd, vector<string> file_names,const map<string, int> &file_to_size, string path
    int thread_num_depth2 = 0;

    for (auto iter = clientid_to_files.begin(); iter != clientid_to_files.end(); iter++)
    {
        auto iter_check = id_to_fd_ult.find(iter->first);
        // TODO : remove assert
        // cout<< "Spawned thread for client private id depth2:" << iter->first <<":"<<iter_check->second<< endl;

        int fd = iter_check->second;

        file_transfer_threads_depth2[thread_num_depth2] = std::thread(send_recv_files_depth2, fd, iter->second, file_to_size_depth2_client, string(argv[2]));
        //cout << fd <<string(argv[2])<<endl;
        thread_num_depth2++;
        // cout << "id2fd" << endl;
        // cout << iter->first << "," << fd << endl;
    }
    struct pollfd pfds_ult[num_depth2_neigh];
    for (int i = 0; i < num_depth2_neigh; i++)
    {

        pfds_ult[i].fd = server_sockfd_depth2[i];
        pfds_ult[i].events = POLLIN;
    }

    // cout << "------------------------------F6:" << num_depth2_req<< endl;
    sleep(1);

    int curr_req_depth2 = 0;
    while (curr_req_depth2 < num_depth2_req)
    {

        int poll_count = poll(pfds_ult, num_depth2_neigh, -1);
        memset(buf, '\0', sizeof buf);
        if (poll_count == -1)
        {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for (int i = 0; i < num_depth2_neigh; i++)
        {
            // Check if someone's ready to read
            if (pfds_ult[i].revents & POLLIN)
            { // We got one!!
                char in_buf[200];
                int nbytes = recv(pfds_ult[i].fd, in_buf, sizeof in_buf, 0);
                if (!in_buf)
                {
                    //cout << "NULL" << endl;
                    continue;
                }
                if (nbytes == 0)
                {
                    continue;
                }
                // cout << "recv : " << in_buf << " from " << pfds_ult[i].fd << endl;

                string in_buf_str = string(in_buf);

                string type = in_buf_str.substr(0, 5);

                string file_req_str = in_buf + 6 * sizeof(char);
                // cout<<"Requested file: "<< file_req_str << endl;

                // char file_size_send[FILE_SIZE_BUFFER];
                // auto iter_file_size = file_to_size.find(file_req_str);

                // if(iter_file_size != file_to_size.end())
                // {
                //     sprintf(file_size_send, "%d:", iter_file_size->second);
                // }

                // send(pfds2[i].fd, file_size_send, sizeof file_size_send, 0);

                FILE *req_file = fopen((string(argv[2]) + file_req_str).c_str(), "rb");
                int bytes_read;
                char chunk_buffer[FILE_BUFFER];
                // cout<<"Sending file: "<<file_req_str<<endl;

                while (!feof(req_file))
                {
                    if ((bytes_read = fread(&chunk_buffer, 1, FILE_BUFFER, req_file)) > 0)
                    {
                        // cout<<"Byte read:"<<bytes_read<<"," <<pfds_ult[i].fd<<endl;
                        if (send(pfds_ult[i].fd, chunk_buffer, bytes_read, 0) == -1)
                        {
                            cout << "Error sending" << endl;
                        }
                    }
                    else
                    {
                        // cout<<"End:"<<bytes_read<< endl;
                        break;
                    }
                    char in_buf[200];
                    int numbytes = recv(pfds_ult[i].fd, in_buf, sizeof in_buf, 0);
                    // cout << "in_buf: " << in_buf<< endl;
                    if (!strcmp(in_buf, "_ACK_"))
                    {
                        continue;
                    }
                    else
                    {
                        // cout << "NO ACK" << endl;
                        break;
                    }
                }
                // cout<<"Sent file: "<<file_req_str<<endl;
                curr_req_depth2++;

                fclose(req_file);
            } // END got ready-to-read from poll()
        }     // END looping through file descriptors
    }

    // cout << "------------------------------F7" << endl;
    sleep(1);

    for (int join_num = 0; join_num < thread_num_depth2; join_num++)
    {
        file_transfer_threads_depth2[join_num].join();
    }

    // cout << "------------------------------F8" << endl;
    sleep(1);
    //cout << "Reached end" << endl;
    for (string file : files)
    {
        auto check_iter = receipts.find(file);
        if (check_iter != receipts.end())
        {
            printf("Found %s at %d with MD5 %s at depth 1\n", file.c_str(), check_iter->second, getMD5_hash("./" + string(argv[2]) + "Downloaded/" + file));
        }
        else if (files_at_depth2.find(file) != files_at_depth2.end())
        {
            printf("Found %s at %d with MD5 %s at depth 2\n", file.c_str(), files_at_depth2.find(file)->second[0].first, getMD5_hash("./" + string(argv[2]) + "Downloaded/" + file));
        }
        else
        {
            cout << "Found " << file << " at 0 with MD5 0 at depth 0" << endl;
        }
    }

    fflush(stdout);
}