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

    // cout<< "File path for MD_Hash: "<<file_path << endl;
    file_descript = open(file_path.c_str(), O_RDONLY);
    if (file_descript < 0)
    {
        fprintf(stderr, "Unable to open file: %d\n", file_descript);
        return "\0";
    }
    // cout<< "Outside if "<<endl;

    file_size = get_size_by_fd(file_descript);

    // cout<<"Caluclated file size: "<< file_size<<endl;

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

void send_recv_files(int fd, vector<string> file_names, const map<string, int> &file_to_size, string path)
{
    char send_buf[200];
    // cout<<"FD :" <<fd<<endl;
    // cout<<"file_names.size()"<< file_names.size() <<endl;

    auto ret = fs::create_directories(path + "Downloaded");
    if (ret)
    {
        // cout << "created directory tree" << endl;
    }
    else
    {
        // cout << "create_directories() failed" << endl;
    }

    for (string file_name : file_names)
    {

        sprintf(send_buf, "_REQ_:%s", file_name.c_str());
        // cout << "sent: " << send_buf <<  endl;
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

        // cout<<"File size to be received:" <<file_size << endl;
        char text[FILE_BUFFER];
        FILE *download_file = fopen((path + "Downloaded/" + file_name).c_str(), "wb");
        int received_size = 0;
        // cout<<"Receiving file: "<< file_name << endl;
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
        // cout<<"File received" << endl;
    }
    return;
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

    vector<string> files;

    for (int i = 0; i < num_files; i++)
    {
        string temp;
        config_file >> temp;
        files.push_back(temp);
    }
    sort(files.begin(), files.end());

    // ---------------

    // scanninf files with the the client itself
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

    char PORT[PORT_SIZE];
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
    std::map<int, int> clientid_to_fd;
    std::map<string, int> file_to_size;

    std::map<pair<string, int>, bool> responses2;
    int num_connections2 = 0;
    bool term = false;
    int expected_req = 0;

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
                // cout << "recv : " << in_buf << " from " << i << endl;

                bool found_flag = false;
                if (i < num_neighbours)
                {
                    for (int j = 0; j < self_files.size(); j++)
                    {
                        if (!strcmp(in_buf, self_files[j].c_str()))
                        {
                            // cout << in_buf << "Yes" << endl;
                            found_flag = true;
                            std::filesystem::path fd_path{(string(argv[2]) + self_files[j]).c_str()};
                            int size_of_file = static_cast<int>(std::filesystem::file_size(fd_path));
                            memset(send_buf, '\0', sizeof send_buf);
                            sprintf(send_buf, "%s,%d,%d", self_files[j].c_str(), self_private_id, size_of_file);
                            send(pfds2[i].fd, send_buf, sizeof send_buf, 0);
                            memset(send_buf, '\0', sizeof send_buf);

                            expected_req++;
                            break;
                        }
                    }
                    if (!found_flag)
                    {
                        memset(send_buf, '\0', sizeof send_buf);
                        sprintf(send_buf, "%s,%d,%d", string(in_buf).c_str(), -self_private_id, 0);
                        send(pfds2[i].fd, send_buf, sizeof send_buf, 0);
                        memset(send_buf, '\0', sizeof send_buf);
                    }
                }
                else
                {
                    // cout << in_buf << endl;
                    char *token, *recv_file_name;
                    int recv_file_size;
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

                    // TODO: Handle cases when private id is zero
                    if (other_private_id_int > 0)
                    {
                        // printf("DEBUG : Found %s at %d with MD5 0 at depth 1\n", recv_file_name, other_private_id_int);
                        // fflush(stdout);
                        token = strtok(NULL, ",");
                        if (!token)
                        {
                            continue;
                        }

                        recv_file_size = stoi(token);
                        // cout<<recv_file_name<<":"<<recv_file_size<<endl;

                        responses2.insert({{string(recv_file_name), other_private_id_int}, true});
                        clientid_to_fd.insert({other_private_id_int, client_sockfd[i - num_neighbours]});
                        auto iter_check = file_to_size.find(string(recv_file_name));
                        // TODO: Consider different file sizes for same name with different clients
                        if (iter_check == file_to_size.end())
                        {
                            file_to_size.insert({string(recv_file_name), recv_file_size});
                        }
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
    std::map<string, vector<int>> rejections;

    for (auto iter = responses2.begin(); iter != responses2.end(); iter++)
    {
        // cout << iter->first.first << iter->first.second << iter->second << endl;
        // printf("%s,%d\n", iter->first.first.c_str(), iter->first.second);
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
                    auto rej_check_iter = rejections.find(iter->first.first);
                    if (rej_check_iter == rejections.end())
                    {
                        // cout<<"inserting rej"<< endl;
                        auto iter_check = clientid_to_fd.find(iter2->second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd.end());
                        int fd = iter_check->second;
                        vector<int> temp = {fd};
                        rejections.insert({iter->first.first, temp});
                    }
                    else
                    {
                        auto iter_check = clientid_to_fd.find(iter2->second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd.end());
                        int fd = iter_check->second;
                        rej_check_iter->second.push_back(fd);
                    }
                    receipts.erase(iter2);
                    receipts.insert({iter->first.first, iter->first.second});
                }
                else if (iter2->second < iter->first.second)
                {
                    auto rej_check_iter = rejections.find(iter->first.first);
                    if (rej_check_iter == rejections.end())
                    {
                        // cout<<"inserting rej"<< endl;
                        auto iter_check = clientid_to_fd.find(iter->first.second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd.end());
                        int fd = iter_check->second;
                        vector<int> temp = {fd};
                        rejections.insert({iter->first.first, temp});
                    }
                    else
                    {
                        auto iter_check = clientid_to_fd.find(iter->first.second);
                        // TODO : remove assert
                        assert(iter_check != clientid_to_fd.end());
                        int fd = iter_check->second;
                        rej_check_iter->second.push_back(fd);
                    }
                }
            }
        }
        else
        {
        }
    }

    // cout<< "Printing rejection list"<<endl;
    // for (auto iter = rejections.begin(); iter != rejections.end(); iter++)
    // {
    //     cout << iter->first<<endl;
    //     for(auto elem : iter->second)
    //     {
    //         // cout<<elem <<","<<endl;
    //     }
    // }

    // fflush(stdout);

    // Startng file transfer part
    // Clients would ask for the files which were found to the respective servers
    // Servers upon receiving the request would start sending the file.

    // cout << "----------------------------F1" << endl;

    // maps client id to the files that it needs to be asked for
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

    std::thread file_transfer_threads[static_cast<int>(id_to_files.size())];
    // spawning threads to get the required files
    int thread_num = 0;
    for (auto iter = id_to_files.begin(); iter != id_to_files.end(); iter++)
    {
        auto iter_check = clientid_to_fd.find(iter->first);
        // TODO : remove assert
        assert(iter_check != clientid_to_fd.end());
        // cout<< "Spawned thread for client private id :" << iter->first <<":"<<iter_check->second<< endl;

        int fd = iter_check->second;

        file_transfer_threads[thread_num] = std::thread(send_recv_files, iter_check->second, iter->second, file_to_size, string(argv[2]));

        thread_num++;
    }

    // sending rejections
    for (auto elem : rejections)
    {
        for (auto fd : elem.second)
        {
            memset(send_buf, '\0', sizeof send_buf);
            sprintf(send_buf, "_REJ_");
            send(fd, send_buf, sizeof send_buf, 0);
        }
    }

    // cout << "----------------------------F2" << endl;

    struct pollfd pfds3[num_neighbours];
    // server sending the requested files
    for (int i = 0; i < num_neighbours; i++)
    {
        pfds3[i].fd = server_sockfd[i];
        pfds3[i].events = POLLIN;
    }

    // cout << "----------------------------F3" << endl;

    // cout << "Server: expected req/rej: " << expected_req << endl;
    int count_req = 0;
    while (count_req < expected_req)
    {
        //cout << "B4" << endl;
        int poll_count = poll(pfds3, num_neighbours, -1);
        //cout << "After" << endl;
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
            if (pfds3[i].revents & POLLIN)
            { // We got one!!
                // cout<< "Inside if"<<endl;
                char in_buf[200];
                int nbytes = recv(pfds3[i].fd, in_buf, sizeof in_buf, 0);
                if (!in_buf)
                {
                    cout << "NULL" << endl;
                    continue;
                }
                if (nbytes == 0)
                {
                    continue;
                }
                // cout << "recv : " << in_buf << " from " << i << endl;

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
                //cout<<"Requested file: "<< file_req_str << "on fd :"<< pfds3[i].fd<<endl;

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
                        if (send(pfds3[i].fd, chunk_buffer, bytes_read, 0) == -1)
                        {
                            cout << "Error sending" << endl;
                        }
                    }
                    else
                    {
                        // cout<<"Byte read:"<<bytes_read<< endl;
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
        file_transfer_threads[i].join();
    }

    //Printing
    for (string file : files)
    {
        auto check_iter = receipts.find(file);
        if (check_iter == receipts.end())
        {
            cout << "Found " << file << " at 0 with MD5 0 at depth 0" << endl;
        }
        else
        {
            printf("Found %s at %d with MD5 %s at depth 1\n", file.c_str(), check_iter->second, getMD5_hash("./" + string(argv[2]) + "Downloaded/" + file));
        }
    }

    fflush(stdout);
}