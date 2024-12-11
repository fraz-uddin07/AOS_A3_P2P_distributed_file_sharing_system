#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

using namespace std;

const int MAX_INP_BUFFER_SIZE = 4096;           //For Communication Only
const int MAX_OUT_BUFFER_SIZE = 4096;           //For Communication
const int MAX_FILE_BUFFER_SIZE = 16 * 1024;     //For File Transfer 16KB

unordered_map<string, pair<string, string>> download_file_data;         // <file_path, <[D|C], grp_id>>

class Util {
    public:
        static vector<string> tokenize_string (char* c_input_string, const char* sep) {
            char* c_input_string_copy[MAX_INP_BUFFER_SIZE];
            // strcpy(c_inp)
            char* token;
            vector<string> tokenized_str;

            token = strtok(c_input_string, sep);
            while (token != NULL) {
                // cout << token << nl;
                tokenized_str.push_back(string(token));
                token =  strtok(NULL, sep);
            }
            // for (auto& p : tokenized_str) {
            //     cout << p << endl;
            // }
            return tokenized_str;
        }

        static string format_as_hex(unsigned char value) {
            ostringstream oss;
            oss << setw(2) << setfill('0') << hex << static_cast<int>(value);
            return oss.str();
        }
};

typedef struct DownloaderStruct {
    int port;
    int part;
    int group_id;
    const char* src_file_path;
    const char* dest_file_path;
} DownloaderStruct;

string get_client_port (char* file_path) {
    string ip_port_info = string(file_path);
    int idx = ip_port_info.find_last_of(':');
    return ip_port_info.substr(idx + 1);
}

vector<string> calc_SHA1 (string& file_path) {
    FILE* file_ptr;

    if ((file_ptr = fopen(file_path.c_str(), "rb")) == nullptr) {
        cout << "Unable to open file" << endl;
        return {};
    }

    vector<string> hash_strings;
    unsigned char part_hash[SHA_DIGEST_LENGTH];
    SHA_CTX ctx;
    unsigned char buffer[512 * 1024];   // 512KB pieces
    size_t bytes_read;
    string hash_string = "";
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file_ptr)) > 0) {
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, buffer, bytes_read);
        SHA1_Final(part_hash, &ctx);

        string hash_string = "";
        for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
            hash_string += Util::format_as_hex(part_hash[i]);
        }
        hash_strings.push_back(hash_string);
    }

    fclose(file_ptr);
    return hash_strings;
}

// void send_file (char* out_file_buffer, string& file_name, int& peer_socket_descriptor, int part_no) {
//     // cout << "Send file called" << endl;
//     int file_descriptor = open(file_name.c_str(), O_RDONLY);
//     if (file_descriptor == -1) {
//         perror("Error opening file");
//         return;
//     }
//     ssize_t bytes_read = 0;
//     ssize_t bytes_read_total = 0;
//     ssize_t total_bytes_to_read = 512 * 1024;
//     int part = 0;
//     int position = 512 * 1024 * part_no;
//     cout << "SENDING FILE " << file_name << endl;
//     memset(out_file_buffer, 0, MAX_FILE_BUFFER_SIZE);    
//     bool last_chunk = false;

//     while ((bytes_read = pread(file_descriptor, out_file_buffer, MAX_FILE_BUFFER_SIZE, position + bytes_read_total)) > 0) {
//         cout << "Sending part " << part << "to peer" << endl;
//         ++part;
//         bytes_read_total += bytes_read;
//         send(peer_socket_descriptor, out_file_buffer, bytes_read, 0);
//         memset(out_file_buffer, 0, MAX_FILE_BUFFER_SIZE);
//         if (total_bytes_to_read - bytes_read < MAX_FILE_BUFFER_SIZE) {
//             last_chunk = true;
//             break;
//         }
//     }
//     if (last_chunk) {
//         bytes_read = pread(file_descriptor, out_file_buffer, total_bytes_to_read - bytes_read, position + bytes_read_total);
//         send(peer_socket_descriptor, out_file_buffer, bytes_read, 0);
//     }

//     return;
// }

void send_file(char* out_file_buffer, std::string& file_name, int& peer_socket_descriptor, int part_no) {
    int file_descriptor = open(file_name.c_str(), O_RDONLY);
    if (file_descriptor == -1) {
        perror("Error opening file");
        return;
    }

    ssize_t bytes_read = 0;
    ssize_t bytes_read_total = 0;
    ssize_t total_bytes_to_read = 512 * 1024; // 512 KB per part
    int position = 512 * 1024 * part_no;
    bool last_chunk = false;

    memset(out_file_buffer, 0, MAX_FILE_BUFFER_SIZE);

    while (bytes_read_total < total_bytes_to_read && 
           (bytes_read = pread(file_descriptor, out_file_buffer, MAX_FILE_BUFFER_SIZE, position + bytes_read_total)) > 0) {
        
        bytes_read_total += bytes_read;
        send(peer_socket_descriptor, out_file_buffer, bytes_read, 0);
        memset(out_file_buffer, 0, MAX_FILE_BUFFER_SIZE);

        if (bytes_read_total >= total_bytes_to_read) {
            last_chunk = true;
            break;
        }
    }

    if (last_chunk && bytes_read_total < total_bytes_to_read) {
        ssize_t remaining_bytes = total_bytes_to_read - bytes_read_total;
        if (remaining_bytes > 0) {
            bytes_read = pread(file_descriptor, out_file_buffer, remaining_bytes, position + bytes_read_total);
            send(peer_socket_descriptor, out_file_buffer, bytes_read, 0);
        }
    }

    close(file_descriptor);
}


void* peer_handler (void* peer_socket_fd) {
    // cout << "Peer handler function callled" << endl;
    int peer_socket_descriptor = *(int*)peer_socket_fd;
    // const int MAX_FILE_BUFFER_SIZE = 4096;
    char* in_file_buffer = (char*)malloc(MAX_FILE_BUFFER_SIZE * sizeof(char));
    char* out_file_buffer = (char*)malloc(MAX_FILE_BUFFER_SIZE * sizeof(char));
    ssize_t bytes_read = 0;
    string empty_response = "INVALID : Empty input";
    const char* space_sep = " ";

    while(1) {
        memset(in_file_buffer, 0, MAX_FILE_BUFFER_SIZE);
        bytes_read = read(peer_socket_descriptor, in_file_buffer, 4096 - 1);
        if (bytes_read <= 0) {
           // Connection closed or error
            cout << "Peer connection closed or error occurred." << endl;
            break;
        }
        in_file_buffer[bytes_read] = '\0';      //Remove newline character

        cout << "RCVD @Peer " << peer_socket_descriptor << " : " << in_file_buffer << endl;

        if (strlen(in_file_buffer) == 0) {
            strncpy(out_file_buffer, empty_response.c_str(), MAX_FILE_BUFFER_SIZE - 1);
            out_file_buffer[MAX_FILE_BUFFER_SIZE - 1] = '\0';
            send(peer_socket_descriptor, out_file_buffer, strlen(out_file_buffer), 0);
            continue;
        }

        vector<string> tokenized_input = Util::tokenize_string(in_file_buffer, space_sep);

        memset(out_file_buffer, 0, MAX_FILE_BUFFER_SIZE);

        if (tokenized_input[0] == "download_file") {
            // cout << "Download File called" << endl;
            //Format: <download_file> <file_name>
            string file_name = tokenized_input[1];
            int part_no = stoi(tokenized_input[2]);
            send_file(out_file_buffer, file_name, peer_socket_descriptor, part_no);
            cout << "File Sent Successfully" << endl << endl;
        }
        else {
            cout << "ERROR : Invalid Command" << endl;
            string response = "ERROR : Invalid Command";
            send(peer_socket_descriptor, response.c_str(), response.size(), 0);
        }


    }

    return NULL;

}

// void* download_file_from_peer (void* args) {
//     //Initialte connection to Peer
//     cout << "Download func called" << endl;
//     DownloaderStruct* download_data = (DownloaderStruct*)args;
//     int port_to_connect = download_data->port;
//     int file_part = download_data->part;
//     int group_id = download_data->group_id;
//     string src_file_path = download_data->src_file_path;
//     string dest_file_path = download_data->dest_file_path;
//     int peer_socket_descriptor;
//     int conn_status;
//     int bytes_read;
//     int total_bytes_transferred = 0;

//     char input_msg_buff[MAX_FILE_BUFFER_SIZE];
//     char exit_str[6] = {'e', 'x', 'i', 't', '\n', '\0'};
//     const char *sep = " ";
//     char output_msg_buff[MAX_FILE_BUFFER_SIZE];
//     struct sockaddr_in peer_address;

//     if ((peer_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//         perror("ERROR : Socket creation failed ");
//         return NULL;
//     }

//     peer_address.sin_family = AF_INET;
//     peer_address.sin_port = htons(port_to_connect);

//     if (inet_pton(AF_INET, "127.0.0.1", &peer_address.sin_addr) <= 0) {
//         perror("ERROR : Invalid address ");
//         return NULL;
//     }

//     if ((conn_status = connect(peer_socket_descriptor, (struct sockaddr*)&peer_address, sizeof(peer_address))) < 0) {
//         perror("ERROR : Connection to Tracker Failed ");
//         return NULL;
//     }

//     cout << "Connected to PEER @" << port_to_connect << "..." << "Starting Data Transfer" << endl;
//     string probe_str = "download_file " + src_file_path + " " + to_string(file_part);
//     ssize_t sent_bytes = send(peer_socket_descriptor, probe_str.c_str(), probe_str.size(), 0);
//     if (sent_bytes == -1) {
//         perror("ERROR : Unable to send message to Server");
//         return NULL;
//     }

//     //Download File from Peer
//     int rec_fd = open(dest_file_path.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
//     // download_file_data.insert({src_file_path, {"D", group_id}});
//     // cout << "File Created" << endl;
//     if (rec_fd < 0) { return NULL; }

//     int down_part_no = 0;
//     int pos = file_part * 512 * 1024;
//     while(1) {
//         memset(input_msg_buff, 0, MAX_FILE_BUFFER_SIZE);
//         bytes_read = read(peer_socket_descriptor, input_msg_buff, MAX_FILE_BUFFER_SIZE);
//         cout << "Downloading part " << down_part_no++ << endl;
//         if (bytes_read <= 0) {
//             break;
//         }
//         else if (bytes_read > 0 && bytes_read  == MAX_FILE_BUFFER_SIZE) {
//             pwrite(rec_fd, input_msg_buff, bytes_read, pos);
//             pos += bytes_read;  
//             total_bytes_transferred += bytes_read;

//         }
//     }
//     cout << "PART " << file_part << " DOWNLOADED...Bytes transferred " << total_bytes_transferred << endl;
//     // download_file_data.erase(src_file_path);
//     // download_file_data.insert({src_file_path, {"C", group_id}});
//     // cout << "File downloaded successfully. " << total_bytes_transferred << " bytes transferred" << endl;
//     return NULL;
// }

void* download_file_from_peer(void* args) {
    DownloaderStruct* download_data = (DownloaderStruct*)args;
    int port_to_connect = download_data->port;
    int file_part = download_data->part;
    std::string src_file_path = download_data->src_file_path;
    std::string dest_file_path = download_data->dest_file_path;
    int peer_socket_descriptor;

    char input_msg_buff[MAX_FILE_BUFFER_SIZE];
    struct sockaddr_in peer_address;

    if ((peer_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR: Socket creation failed ");
        return NULL;
    }

    peer_address.sin_family = AF_INET;
    peer_address.sin_port = htons(port_to_connect);
    if (inet_pton(AF_INET, "127.0.0.1", &peer_address.sin_addr) <= 0) {
        perror("ERROR: Invalid address ");
        return NULL;
    }

    // if (connect(peer_socket_descriptor, (struct sockaddr*)&peer_address, sizeof(peer_address)) < 0) {
    //     perror("ERROR: Connection to Peer Failed ");
    //     return NULL;
    // }
    const int MAX_RETRIES = 10;
    const int RETRY_DELAY_MS = 500; 
    int conn_status;
    bool connected = false;
    struct timespec delay;
    delay.tv_sec = RETRY_DELAY_MS / 1000;
    delay.tv_nsec = (RETRY_DELAY_MS % 1000) * 1000000;  
    for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        conn_status = connect(peer_socket_descriptor, (struct sockaddr*)&peer_address, sizeof(peer_address));
        if (conn_status == 0) {
            connected = true;
            break;
        } 
        else {
            perror("ERROR: Connection to Peer Failed");
            nanosleep(&delay, NULL);  // Sleep before retrying
        }
    }

    if (!connected) {
        std::cerr << "ERROR: Could not connect to Peer after " << MAX_RETRIES << " attempts" << std::endl;
        close(peer_socket_descriptor);
        return NULL;
    }


    std::cout << "Connected to PEER @" << port_to_connect << "..." << std::endl;

    std::string probe_str = "download_file " + src_file_path + " " + std::to_string(file_part);
    ssize_t sent_bytes = send(peer_socket_descriptor, probe_str.c_str(), probe_str.size(), 0);
    if (sent_bytes == -1) {
        perror("ERROR: Unable to send message to Server");
        return NULL;
    }

    int rec_fd = open(dest_file_path.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
    if (rec_fd < 0) {
        perror("ERROR: Unable to open destination file");
        return NULL;
    }

    int pos = file_part * 512 * 1024;
    while (true) {
        memset(input_msg_buff, 0, MAX_FILE_BUFFER_SIZE);
        ssize_t bytes_read = read(peer_socket_descriptor, input_msg_buff, MAX_FILE_BUFFER_SIZE);
        
        if (bytes_read <= 0) break;

        pwrite(rec_fd, input_msg_buff, bytes_read, pos);
        pos += bytes_read;
    }

    close(rec_fd);
    close(peer_socket_descriptor);
    std::cout << "PART " << file_part << " DOWNLOADED..." << std::endl;
    return NULL;
}


void* peer_connection_handler (void* CLIENT_PORT_NO) {

    int CLIENT_PORT = *(int*)CLIENT_PORT_NO;
    int peer_socket_descriptor;
    int opt = 1;
    struct sockaddr_in client_address;
    socklen_t client_addr_len = sizeof(client_address);
    if ((peer_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR : Peer Socket creation failed ");
        // exit(EXIT_FAILURE);
    }
    if (setsockopt(peer_socket_descriptor, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("ERROR : Setsockopt for Peer connection Failed");
        // exit(EXIT_FAILURE);
    }
    client_address.sin_family = AF_INET;  //IPv4
    client_address.sin_addr.s_addr = INADDR_ANY;
    client_address.sin_port = htons(CLIENT_PORT);
    if (bind(peer_socket_descriptor, (struct sockaddr*)&client_address, sizeof(client_address)) < 0) {
        perror("ERROR : Socket bind for Peer connection failed");
        // exit(EXIT_FAILURE);
    }
    //Listen for connection attempts by other peers
    if (listen(peer_socket_descriptor, 15) < 0) {
        perror("ERROR : Listening Falied");
        // exit(EXIT_FAILURE);
    }
    while(1) {
        int* new_peer_socket_descriptor = (int*)malloc(1 * sizeof(int));

        //Accept incoming peer requests
        if ((*new_peer_socket_descriptor = accept(peer_socket_descriptor, (struct sockaddr*)&client_address, (socklen_t*)&client_addr_len)) < 0) {
            perror("ERROR : Unable to accept connection from peer");
            exit(EXIT_FAILURE);
        }
        cout << "New Peer connected with id " << *new_peer_socket_descriptor << endl;

        pthread_t new_peer_thread;
        if (pthread_create(&new_peer_thread, NULL, peer_handler, (void*)new_peer_socket_descriptor) != 0) {
            perror ("ERROR : Unable to create thread for new Peer");
            delete new_peer_socket_descriptor;
        }
        else {
            pthread_detach(new_peer_thread);
        }
    }

    close(peer_socket_descriptor);
    return NULL;

}

uint16_t get_port_from_file (string& file_path, int tracker_no) {
    ifstream file_ptr(file_path);
    if (!file_ptr.is_open()) {
        return uint16_t(8080);
    }
    string s; int cnt_l = 0;
    while (getline(file_ptr, s)) {
        if (cnt_l == tracker_no) {
            break;
        }
        ++cnt_l;
    }
    int idx = s.find_last_of(':');
    return uint16_t(stoi(s.substr(idx + 1)));
}

string get_address_from_file (string& file_path, int tracker_no) {
    ifstream file_ptr(file_path);
    if (!file_ptr.is_open()) {
        return "127.0.0.1";
    }
    string s; int cnt_l = 0;
    while (getline(file_ptr, s)) {
        if (cnt_l == tracker_no) {
            break;
        }
        ++cnt_l;
    }
    int idx = s.find_last_of(':');
    return s.substr(0, idx);
}

void create_file_with_size(const char *filename, size_t size) {
    // Open the file for writing, create it if it doesn't exist
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        perror("Error opening file");
        return;
    }

    // Use lseek to set the file size
    if (lseek(fd, size - 1, SEEK_SET) == -1) {
        perror("Error seeking in file");
        close(fd);
        return;
    }

    // Write a single byte to the end to allocate the space
    if (write(fd, "", 1) != 1) {
        perror("Error writing to file");
        close(fd);
        return;
    }

    close(fd);
}



int main (int argc, char* argv[]) {

    //Creating thread for Peer Connections
    string client_port_str = get_client_port(argv[1]);

    int* CLIENT_PORT = (int*)malloc(1 * sizeof(int));
    *CLIENT_PORT = stoi(client_port_str);
    pthread_t peer_listen_thread;
    if (pthread_create(&peer_listen_thread, NULL, peer_connection_handler, (void*)CLIENT_PORT) != 0) {
        perror ("ERROR : Unable to create PEER_THREAD");
    }
    else {
        pthread_detach(peer_listen_thread);
    }

    //Initiating connection to Tracker
    string file_path = string(argv[2]);
    uint16_t DESTPORT = get_port_from_file(file_path, 0);   //Tracker Port no
    string tracker_ip_address = get_address_from_file(file_path, 0);

    int client_socket_descriptor;
    int conn_status;
    int bytes_read;

    char input_msg_buff[MAX_INP_BUFFER_SIZE];
    char exit_str[6] = {'e', 'x', 'i', 't', '\n', '\0'};
    const char *sep = " ";
    char output_msg_buff[MAX_OUT_BUFFER_SIZE];
    struct sockaddr_in tracker_address;

    if ((client_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR : Socket creation failed ");
        exit(EXIT_FAILURE);
    }

    tracker_address.sin_family = AF_INET;
    tracker_address.sin_port = htons(DESTPORT);

    if (inet_pton(AF_INET, tracker_ip_address.c_str(), &tracker_address.sin_addr) <= 0) {
        perror("ERROR : Invalid address ");
        exit(EXIT_FAILURE);
    }

    if ((conn_status = connect(client_socket_descriptor, (struct sockaddr*)&tracker_address, sizeof(tracker_address))) < 0) {
        perror("ERROR : Connection to Tracker Failed ");
        exit(EXIT_FAILURE);
    }

    cout << "Connected to SERVER@" << DESTPORT << endl << endl;

    string prompt_str = ":$>";
    while(1) {
        string input = "";
        cout << prompt_str << " ";
        memset(output_msg_buff, 0, MAX_OUT_BUFFER_SIZE);
        if (fgets(output_msg_buff, sizeof(output_msg_buff), stdin) != NULL) {
            // printf("You entered: %s", output_msg_buff);
        }
        else {
            cout << "Error reading output" << endl;
            continue;
        }
        if (strlen(output_msg_buff) == 1) { continue; }
        output_msg_buff[strlen(output_msg_buff) - 1] = '\0';
        input = string(output_msg_buff);
        vector<string> tokenized_string = Util::tokenize_string(output_msg_buff, sep);

        //Process input command
        if (tokenized_string[0] == "upload_file") {
            const int PIECE_SIZE = 524288;      // 512KB pieces
            vector<string> partwise_hashes = calc_SHA1(tokenized_string[1]);
            if (partwise_hashes.size() == 0) {
                cout << "Error opening file" << endl;
                continue;
            }
            int input_file_descriptor = open(tokenized_string[1].c_str(), O_RDONLY);
            struct stat input_file_stat_buff;
            if (fstat(input_file_descriptor, &input_file_stat_buff) == -1) {
                cout << "Error in creating file_statistics buffer";
                continue;
            }

            input = input + " " + client_port_str + " " + to_string(input_file_stat_buff.st_size) + " ";

            //append all hashes
            for (int i = 0; i < partwise_hashes.size(); i++) {
                input = input + partwise_hashes[i] + " ";
            }
            cout << input << endl;












            // cout << "hash : " << hash << endl;
            // continue;
        }
        else if (tokenized_string[0] == "download_file") {
            //Send command to server
            ssize_t sent_bytes = send(client_socket_descriptor, input.c_str(), input.size(), 0);
            memset(input_msg_buff, 0, MAX_INP_BUFFER_SIZE);
            bytes_read = read(client_socket_descriptor, input_msg_buff, MAX_INP_BUFFER_SIZE - 1);
            input_msg_buff[bytes_read] = '\0';

            cout << "RCVD : " << input_msg_buff << endl;
            //Tokenize server response
                //Format: <OK> <port_no> <sha1_hash>  OR  <ERROR> <...>
            vector<string> tokenized_download_info = Util::tokenize_string(input_msg_buff, sep);
            if (tokenized_download_info[0] == "OK") {
                vector<pair<int, string>> file_download_data;
                int64_t file_size = stoi(tokenized_download_info[1]);
                for (int i = 2; i < tokenized_download_info.size(); i++){
                    int idx = tokenized_download_info[i].find_last_of('_');
                    int tmp_port = stoi(tokenized_download_info[i].substr(0, idx));
                    string tmp_hash = tokenized_download_info[i].substr(idx + 1);
                    file_download_data.push_back({tmp_port, tmp_hash});
                }
                for (auto p : file_download_data) {
                    cout << p.first << " " << p.second << endl;
                }
                cout << endl << endl;

                //Create the file with desired size
                create_file_with_size(tokenized_string[3].c_str(), file_size);

                //
                pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

                pthread_mutex_lock(&mutex);
                download_file_data.insert({tokenized_string[2], {"D", tokenized_string[1]}});
                pthread_mutex_unlock(&mutex);


                for (int i = 0; i < file_download_data.size(); i++) {
                    DownloaderStruct* download_args = (DownloaderStruct*)malloc(sizeof(DownloaderStruct));
                    download_args->part = i;
                    download_args->port = file_download_data[i].first;
                    download_args->group_id = stoi(tokenized_string[1]);
                    download_args->src_file_path = tokenized_string[2].c_str();
                    download_args->dest_file_path = tokenized_string[3].c_str();

                    pthread_t download_thread;
                    if (pthread_create(&download_thread, NULL, download_file_from_peer, (void*)download_args)) {
                        perror ("ERROR : Unable to create thread for new Peer");
                        delete download_args;
                    }
                    else {
                        pthread_detach(download_thread);
                    }


                }

                download_file_data.erase(tokenized_string[2]);
                download_file_data.insert({tokenized_string[2], {"C", tokenized_string[1]}});












                // cout << "Connecting to Peer@" << tokenized_download_info[1] << ":" << endl;
                // if(download_file_from_peer(tokenized_download_info[1], tokenized_string[1], tokenized_string[2], tokenized_string[3])) {
                //     // cout << "Complete File Downloaded from Peer Successfully" << endl;
                // }
                // else {
                //     cout << "ERROR : Error downloading file" << endl;
                // }
            }
            else {
                cout << "Received Message : " << input_msg_buff << endl << endl;
            }
            continue;
        }
        else if (tokenized_string[0] == "show_downloads") {
            for (auto it = download_file_data.begin(); it != download_file_data.end(); it++) {
                // [D|C] group_id file_name
                cout << it->second.first << " " << it->second.second << " " << it->first << endl; 
            }
            continue;
        }

        // ssize_t sent_bytes = send(client_socket_descriptor, output_msg_buff, strlen(output_msg_buff), 0);
        ssize_t sent_bytes = send(client_socket_descriptor, input.c_str(), input.size(), 0);
        if (sent_bytes == -1) {
            perror("ERROR : Unable to send message to Server");
            continue;
        }
        // cout << "SENT TRCKR@" << DESTPORT << ": ";
        // cout << input << endl;

        // memset(input_msg_buff, 0, sizeof(input_msg_buff));
        cout << "Trying to read " << endl; 
        bytes_read = read(client_socket_descriptor, input_msg_buff, MAX_INP_BUFFER_SIZE - 1);
        cout << "bytes_read " << bytes_read << endl;
        input_msg_buff[bytes_read] = '\0';
        if (bytes_read == 0) { continue; }
        cout << "RCVD : " << input_msg_buff << endl << endl;
        // cout << "Reaponse size " << strlen(input_msg_buff) << endl;

        // if (strcmp(output_msg_buff, exit_str) == 0) {
        //     break;
        // }
        if (string(input_msg_buff) == "exit") {
            cout << "EXITING" << endl;
            break;
        }
    
    
    }



    close(client_socket_descriptor);

    return 0;

}