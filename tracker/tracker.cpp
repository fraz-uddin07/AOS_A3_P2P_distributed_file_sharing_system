#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>
#include <fstream>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

using namespace std;

class Util {
    public:
        static vector<string> tokenize_string (char* c_input_string, const char* sep) {
            // char* c_input_string = Util::convert_to_cstr(input_str);
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

        static void push_to_out_buffer (string out_msg, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE) {
            memset(output_msg_buff, 0, sizeof(output_msg_buff));
            int i = 0;
            for (i = 0; i < min((int)out_msg.size(), MAX_OUT_BUFF_SIZE); i++) {
                output_msg_buff[i] = out_msg[i];
            }
            out_msg[i] = '\0';
        }

        static bool is_exit(char* string1, char* string2) {
            for (int i = 0; i < 4 ; i++) {
                if (string1[i] != string2[i]) {
                    return false;
                }
            }
            return true;
        }

};

bool shutdown_server = false;
const int MAX_INP_BUFF_SIZE = 4096;
const int MAX_OUT_BUFF_SIZE = 4096;


typedef struct User {
    int user_id;
    string password;
    bool is_online;

    User() : user_id(0), password(""), is_online(false) {}

    User(int user_id, string password, bool is_online) { 
        this->user_id = user_id; 
        this->password = password; 
        this->is_online = is_online; }
} User;


typedef struct Group {
    int owner_id;
    int group_id;
    set<int> members;
    set<int> pending_join_requests;
    unordered_map<string, vector<string>> shared_files;

    Group() : owner_id(0), group_id(0) {}

    Group(int owner_id, int group_id) {
        this->owner_id = owner_id;
        this->group_id = group_id;
        this->members.insert(owner_id);
    }

} Group;

typedef struct FIleInfo {
    string file_name;
    int file_size;
    int num_parts;
    vector<vector<pair<int, string>>> part_data;
} FileInfo;


unordered_map<int, User> active_users;                      // <user_id, user_data>
unordered_map<int, Group> active_groups;                    // <group_id, group_data>
unordered_map<int, int> logged_in;                          // <socket_id, user_id>
unordered_map<string, pair<string, string>> sha1_hash;      // <file_path, <port, sha1_hash>>      //Obsolete
unordered_map<string, FIleInfo> file_info;                  // <file_path, FileInfo>



bool argument_validator (vector<string>& tokenized_string) {
    string command = tokenized_string[0];

    if (command == "create_user") {
        return tokenized_string.size() == 3 ? true : false; 
    }
    else if (command == "login") {
        return tokenized_string.size() == 3 ? true : false; 
    }
    else if (command == "create_group") {
        return tokenized_string.size() == 2 ? true : false; 
    }
    else if (command == "join_group") {
        return tokenized_string.size() == 2 ? true : false; 
    }
    else if (command == "leave_group") {
        return tokenized_string.size() ==  2 ? true : false; 
    }
    else if (command == "list_requests") {
        return tokenized_string.size() == 2 ? true : false; 
    }
    else if (command == "accept_request") {
        return tokenized_string.size() == 3 ? true : false; 
    }
    else if (command == "list_groups") {
        return tokenized_string.size() == 1 ? true : false; 
    }
    else if (command == "logout") {
        return tokenized_string.size() == 1 ? true : false; 
    }
    else if (command == "upload_file") {
        return tokenized_string.size() >= 5 ? true : false; 
    }
    else if (command == "download_file") {
        return tokenized_string.size() == 4 ? true : false; 
    }
    else if (command == "list_files") {
        return tokenized_string.size() == 2 ? true : false; 
    }
    return false;

}


int get_online_user (int client_socket_descriptor) {
    if (logged_in.find(client_socket_descriptor) != logged_in.end()) {
        return logged_in[client_socket_descriptor];
    }
    return -1;
}

void print_all_users () {
    for (unordered_map<int, User>::iterator it = active_users.begin(); it != active_users.end(); it++) {
        int user_id = it->first;
        User user_data = it->second;
        cout << user_id << " " << user_data.password << " " << user_data.is_online << endl;
    }
    return;
}

void print_all_groups () {
    for (unordered_map<int, Group>::iterator it = active_groups.begin(); it != active_groups.end(); it++) {
        int group_id = it->first;
        Group& group_data = it->second;
        cout << "Group_ID : " << group_id << " | Owner_ID : " << group_data.owner_id << " | Members : " << group_data.members.size() << " | Pending Join req : " << group_data.pending_join_requests.size() << endl;
    }
    return;
}

void list_groups (char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE) {
    string response = "";
    for (unordered_map<int, Group>::iterator it = active_groups.begin(); it != active_groups.end(); it++) {
        int group_id = it->first;
        Group& group_data = it->second;
        response += "Group_ID~:~" + to_string(group_id) + "~Owner~:~" + to_string(group_data.owner_id) + " "; 
        cout << "Group_ID : " << group_id << " Owner : " << group_data.owner_id << endl;
    }
    if (response.size() == 0) { response = "No Groups Found"; }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void create_user (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE) {
    string response = "";
    int user_id = stoi(tokenized_string[1]);
    if (active_users.find(user_id) != active_users.end()) {
        response = "ERROR : User already Exists";
        cout << "User already Exists" << endl;
    }
    else {
        User user(user_id, tokenized_string[2], false);
        active_users.insert({user_id, user});
        response = "OK : User Account Created"; 
        cout << "User Account Created" << endl;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    print_all_users();
    return;
}

void login_user (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, const int client_socket_descriptor) {
    string response = "";
    int user_id = stoi(tokenized_string[1]);
    if (logged_in.find(client_socket_descriptor) != logged_in.end()) {
        cout << "A user is already logged in from this client." << endl;
        response = "ERROR : A User is already logged in";
    }
    else if (active_users.find(user_id) != active_users.end()) {
        User& user_data = active_users[user_id];
        if (user_data.password != tokenized_string[2]) {
            response = "ERROR : Incorrect Password";
            cout << "ERROR : Incorrect Password" << endl;
        }
        else if (user_data.is_online) {
            response = "INVALID : User is already Logged in"; 
            cout << "User is already Logged in" << endl;
        }
        else {
            user_data.is_online = true;
            logged_in.insert({client_socket_descriptor, user_id});
            response = "OK : User Logged in"; 
            cout << "User Login Successfull" << endl;
        }
    }
    else {
        response = "ERROR : No such User exists"; 
        cout << "ERROR : No such User exists" << endl;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    print_all_users();
    return;
}

void create_group (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    int group_id = stoi(tokenized_string[1]);
    int owner_id = get_online_user(client_socket_descriptor);
    string response = "";

    if (owner_id == -1) {
        response = "ERROR : You must be logged in to create a group";
        cout << "ERROR : You must be logged in to create a group" << endl;
        // return;
    }
    else if (active_groups.find(group_id) != active_groups.end()) {
        response = "INVALID : Group already Exists";
        cout << "Group already Exists" << endl;
    }
    else {
        Group group(owner_id, group_id);
        active_groups.insert({group_id, group});
        response = "OK : Group Created Successfully";
        cout << "Group Created Successfully" << endl;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    print_all_groups();
    return;
}

void join_group (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    string response = "";
    int user_id = get_online_user(client_socket_descriptor);
    if (user_id == -1) {
        response = "ERROR : You must be logged in to join a group";
        cout << "ERROR : You must be logged in to join a group" << endl;
        strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
        output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
        return;
    }
    int group_id = stoi(tokenized_string[1]);

    if (active_groups.find(group_id) != active_groups.end()) {
        set<int>& member_list = active_groups[group_id].members;
        if (member_list.find(user_id) != member_list.end()) {
            response = "INVALID : You are already a member";
            cout << "You are already a member" << endl;
        }
        else {
            active_groups[group_id].pending_join_requests.insert(user_id);
            cout << "Pending join of " << user_id << " for group " << group_id << "added" << endl;
        }
    }
    else {
        response = "ERROR : No such group exists"; 
        cout << "ERROR : No such group exists" << endl;
    }

    

    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

bool remove_member (int& group_id, int& user_id) {
    if (active_groups[group_id].owner_id == user_id) {
        active_groups[group_id].members.erase(user_id);
        if (active_groups[group_id].members.size() == 0) {
            active_groups.erase(group_id);
        }
        else {
            int new_owner_id = *(active_groups[group_id].members.begin());
            active_groups[group_id].owner_id = new_owner_id;
        }
    }
    else {
        active_groups[group_id].members.erase(user_id);
    }
    return true;
}

void leave_group (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    string response = "";
    int user_id = get_online_user(client_socket_descriptor);
    int group_id = stoi(tokenized_string[1]);
    if (user_id == -1) {
        response = "INVALID : You must be logged in to leave a group";
        cout << "INVALID : You must be logged in to leave a group" << endl;
        // return;
    }
    else if (active_groups.find(group_id) == active_groups.end()) {
        response = "INVALID : No such group exists";
        cout << "INVALID : No such group exists" << endl;
        // return;
    }
    else {
        set<int>& member_list = active_groups[group_id].members;
        if (member_list.find(user_id) == member_list.end()) {
            response = "INVALID : You are not a member of this Group";
            cout << "INVALID : You are not a member of this Group" << endl;
        }
        else {
            if (remove_member(group_id, user_id)) {
                response = "SUCCESS : User_ID " + to_string(user_id) + " left Group_ID " + tokenized_string[1];
                cout << "SUCCESS : User_ID " << user_id << " left Group_ID " << stoi(tokenized_string[1]); 
            }
            else {
                response = "ERROR : Unable to process request";
                cout << "ERROR : Unable to process request" << endl;
            }
        }
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void list_requests (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    string response = "";
    int group_id = stoi(tokenized_string[1]);
    int user_id = get_online_user(client_socket_descriptor);

    if (active_groups.find(group_id) == active_groups.end()) {
        response = "ERROR : No such group exists";
    }
    else if (active_groups[group_id].owner_id == user_id) {
        set<int>& pending_requests = active_groups[group_id].pending_join_requests;
        response = "Pending~requests~for~Group_ID~" + to_string(group_id) + " "; 
        cout << "Pending requests for Group_ID " << group_id << endl;
        for (int u_id : pending_requests) {
            response += to_string(u_id) + " ";
            cout << u_id << endl;
        }
    }
    else {
        response = "INVALID : Insufficient Privileges"; 
        cout << "INVALID : Insufficient Privileges" << endl;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void accept_request (vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    string response = "";
    int group_id = stoi(tokenized_string[1]);
    int req_user_id = stoi(tokenized_string[2]);
    int user_id = get_online_user(client_socket_descriptor);
    if (active_groups[group_id].owner_id == user_id) {
        set<int>& pending_requests = active_groups[group_id].pending_join_requests;
        if (pending_requests.find(req_user_id) == pending_requests.end()) {
            response = "INVALID : No such pending requests"; 
            cout << "INVALID : No such pending requests" << endl;
        }
        else {
            active_groups[group_id].members.insert(req_user_id);
            pending_requests.erase(req_user_id);
            response = "OK : Request Accepted";
            cout << "OK : Request Accepted" << endl;
        }
    }
    else {
        response = "INVALID : Insufficient privileges";
        cout << "INVALID : Insufficient privileges" << endl;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void logout_user (char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    string response = "";
    int user_id = get_online_user(client_socket_descriptor);
    if (active_users[user_id].is_online) {
        active_users[user_id].is_online = false;
        logged_in.erase(client_socket_descriptor);
        response = "OK : Logout";
    }
    else {
        response = "INVALID : User is already logged out"; 
        cout << "INVALID : User is already logged out" << endl;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void upload_file(vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    string response = "";
    string file_path = tokenized_string[1];
    int group_id = stoi(tokenized_string[2]);
    int src_port = stoi(tokenized_string[3]);
    int tmp_file_size = stoi(tokenized_string[4]);
    cout << "FIle_sz " << tmp_file_size << endl;
    int num_parts = tokenized_string.size() - 5;
    int user_id;
    vector<string> to_push;
    to_push.push_back(tokenized_string[3]);
    if ((user_id = get_online_user(client_socket_descriptor)) == -1) {
        cout << "User must be logged in" << endl;
        response = "ERROR : User must be logged in";
    }
    else if (active_groups.find(group_id) == active_groups.end()) {
        cout << "No such Group exists" << endl;
        response = "ERROR : No such Group exists";
    }
    else if (active_groups[group_id].members.find(user_id) == active_groups[group_id].members.end()) {
        cout << "You are not a member of this group";
        response = "ERROR : You are not a member of this group";
    }
    else {
        if (active_groups[group_id].shared_files.find(file_path) == active_groups[group_id].shared_files.end()) {
            active_groups[group_id].shared_files.insert({file_path, to_push});
            response = "OK : Inserted file data";
            cout << "map data " << endl;
            for (const auto& entry : active_groups[group_id].shared_files) {
               std::cout << "File Name: " << entry.first << "\nFiles:\n";
                for (const auto& file : entry.second) {
                    std::cout << " - " << file << "\n";
                }
            }
            // unordered_map<string, vector<string>> mp = active_groups[group_id].shared_files;

        }
        //TODO : Handle duplicate file uploads
        else {
            active_groups[group_id].shared_files[file_path].push_back(to_string(user_id));
            response = "OK : Added Peer data";
        }
            cout << "Inserting data..." << endl;

        //Insert in FileInfo
        if (file_info.find(file_path) != file_info.end()) {
            cout << "Inserting data..." << endl;
            int j = 5;
            vector<vector<pair<int, string>>>& temp_part_data = file_info[file_path].part_data;
            temp_part_data.resize(num_parts);
            for (int i = 0; i < temp_part_data.size(); i++, j++) {
                temp_part_data[i].push_back({src_port, tokenized_string[j]});
            }
            cout << "OLD FILE : Added all hashes" << endl;
        }
        else {
            FileInfo temp_file_info;
            temp_file_info.file_name = file_path;
            temp_file_info.num_parts = tokenized_string.size() - 4;
            temp_file_info.file_size = tmp_file_size;
            vector<vector<pair<int, string>>> temp_part_data;
            temp_part_data.resize(num_parts);
            int j = 5; int i = 0;
            for (j = 5, i = 0; j < tokenized_string.size(); j++, i++) {
                temp_part_data[i].push_back({src_port, tokenized_string[j]});
            }
            temp_file_info.part_data = temp_part_data;

            file_info[file_path] = temp_file_info;


            cout << "NEW FILE : Added all hashes" << endl;
        }


















        sha1_hash[file_path].first = tokenized_string[3]; //Port_no
        sha1_hash[file_path].second = tokenized_string[4]; //SHA1 hash
    //calc sha1 hash
    
    //UPload to group

    //upload to sha1 hash
        //Print all 
        cout << "File size " << file_info[file_path].file_size << endl;
        vector<vector<pair<int, string>>>& temp_part_data = file_info[file_path].part_data;
        for (int i = 0; i < temp_part_data.size(); i++) {
            cout << "Part " << i << endl;
            for (int j = 0; j < temp_part_data[i].size(); j++) {
                cout << temp_part_data[i][j].first << " " << temp_part_data[i][j].second << endl;
            }
        }
    }

    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void list_files(vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    int group_id = stoi(tokenized_string[1]);
    string response = "";
    for (auto it = active_groups[group_id].shared_files.begin(); it != active_groups[group_id].shared_files.end(); it++) {
        cout << it->first << endl;
        response += it->first;
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    return;
}

void download_file(vector<string>& tokenized_string, char* output_msg_buff, const int& MAX_OUT_BUFF_SIZE, int client_socket_descriptor) {
    int group_id = stoi(tokenized_string[1]);
    string file_name = tokenized_string[2];
    int user_id;
    string response = "";
    if ((user_id = get_online_user(client_socket_descriptor)) == -1) {
        cout << "User must be logged in" << endl;
        response = "ERROR : User must be logged in";
    }
    else if (active_groups.find(group_id) == active_groups.end()) {
        cout << "No such Group exists" << endl;
        response = "ERROR : No such Group exists";
    }
    else if (active_groups[group_id].members.find(user_id) == active_groups[group_id].members.end()) {
        cout << "You are not a member of this group";
        response = "ERROR : You are not a member of this group";
    }
    else if (active_groups[group_id].shared_files.find(file_name) != active_groups[group_id].shared_files.end()){
        // response = "OK " + sha1_hash[file_name].first + " " + sha1_hash[file_name].second; 
        response = "OK " + to_string(file_info[file_name].file_size) + " ";
        vector<vector<pair<int, string>>>& temp_part_data = file_info[file_name].part_data;
        
        for (int i = 0; i < temp_part_data.size(); i++) {
            srand(time(NULL));
            int random_num = rand() % temp_part_data[i].size();
            response = response + to_string(temp_part_data[i][random_num].first) + "_" + temp_part_data[i][random_num].second + " ";  
        }


        cout << "Port and SHA details sent to client" << endl;
        cout << response << endl;





    }
    else {
        cout << "File does not exist" << endl;
        response = "ERROR : File does not exist";
    }
    strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
    output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
    // cout << "msg buff " << output_msg_buff << endl;
    return;

}


void* client_handler(void* client_socket) {
    int client_socket_descriptor = *(int*)client_socket;
    char* input_msg_buff = (char*)malloc(MAX_INP_BUFF_SIZE * sizeof(char));
    char* output_msg_buff = (char*)malloc(MAX_OUT_BUFF_SIZE * sizeof(char));
    ssize_t bytes_read = 0;
    char* msg = "OK";
    const char* space_separator = " ";
    // char exit_str[6] = {'e', 'x', 'i', 't', '\n', '\0'};
    string response = "INVALID : Empty input";



    while(1) {

        memset(input_msg_buff, 0, MAX_INP_BUFF_SIZE);
        bytes_read = read(client_socket_descriptor, input_msg_buff, MAX_INP_BUFF_SIZE - 1);
        // cout << "bytes_read " << bytes_read << endl;
        input_msg_buff[bytes_read] = '\0';      //Remove newline character

        if (bytes_read == 0) {
            continue;
        }

        cout << "RCVD@Client " << client_socket_descriptor << " : " << input_msg_buff << endl;

        // if (strncmp(input_msg_buff, exit_str, 4) == 0) {
        //     break;
        // }
        memset(output_msg_buff, 0, MAX_OUT_BUFF_SIZE);
        // if (strlen(input_msg_buff) == 0) {
        //     strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
        //     output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
        //     send(client_socket_descriptor, output_msg_buff, strlen(output_msg_buff), 0);
        //     continue;
        // }

        vector<string> tokenized_input = Util::tokenize_string(input_msg_buff, space_separator);


        if (tokenized_input[0] == "create_user") {
            if (argument_validator(tokenized_input)) {
                create_user(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE);
                // cout << "CREATE_USER Called" << endl;
                // push_to_out_buffer("SUCCESS : User created successfully");
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to create_user command"); }
        }
        else if (tokenized_input[0] == "login") {
            // cout << "LOGIN_USER Called" << endl;
            if (argument_validator(tokenized_input)) {
                login_user(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to login command"); }
        }
        else if (tokenized_input[0] == "create_group") {
            // cout << "CREATE_GRP Called" << endl;
            if (argument_validator(tokenized_input)) {
                create_group(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to create_group command"); }
        }
        else if (tokenized_input[0] == "join_group") {
            // cout << "JOIN_GRP Called" << endl;
            if (argument_validator(tokenized_input)) {
                join_group(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to join_group command"); }
        }
        else if (tokenized_input[0] == "leave_group") {
            // cout << "LEAVE_GRP Called" << endl;
            if (argument_validator(tokenized_input)) {
                leave_group(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to leave_group command"); }
        }
        else if (tokenized_input[0] == "list_requests") {
            // cout << "LIST_REQUESTS Called" << endl;
            if (argument_validator(tokenized_input)) {
                list_requests(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to list_requests command"); }
        }
        else if (tokenized_input[0] == "accept_request") {
            // cout << "ACCEPT_REQUESTS Called" << endl;
            if (argument_validator(tokenized_input)) {
                accept_request(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to accept_request command"); }
        }
        else if (tokenized_input[0] == "list_groups") {
            // cout << "LIST_GRPS Called" << endl;
            if (argument_validator(tokenized_input)) {
                list_groups(output_msg_buff, MAX_OUT_BUFF_SIZE);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to list_groups command"); }
        }
        else if (tokenized_input[0] == "logout") {
            // cout << "LOGOUT called" << endl;
            if (argument_validator(tokenized_input)) {
                logout_user(output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to logout command"); }
        }
        else if (tokenized_input[0] == "upload_file") {
            // cout << "Upload file called" << endl;
            if (argument_validator(tokenized_input)) {
                upload_file(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to upload_file command"); }
        }
        else if (tokenized_input[0] == "download_file") {
            // cout << "Upload file called" << endl;
            if (argument_validator(tokenized_input)) {
                download_file(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to download_file command"); }
        }
        else if (tokenized_input[0] == "list_files") {
            cout << "List file called" << endl;
            if (argument_validator(tokenized_input)) {
                list_files(tokenized_input, output_msg_buff, MAX_OUT_BUFF_SIZE, client_socket_descriptor);
            }
            else { strcpy(output_msg_buff, "ERROR : Invalid arguments to list_files command"); }
        }
        else if (tokenized_input[0] == "exit") {
            // cout << "LOGOUT called" << endl;
            // logout_user(output_msg_buff, MAX_OUT_BUFF_SIZE);
            string response = "exit";
            cout << "EXIT Called" << endl;
            strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
            output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
            break;
        }
        else {
            string response = "INVALID Command";
            cout << "INVALID Command" << endl;
            strncpy(output_msg_buff, response.c_str(), MAX_OUT_BUFF_SIZE - 1);
            output_msg_buff[MAX_OUT_BUFF_SIZE - 1] = '\0';
        }
        cout << endl;

        if (strlen(output_msg_buff) == 0) {
            output_msg_buff[0] = 'O';
            output_msg_buff[1] = 'K';
            output_msg_buff[2] = '\0';
        }

        cout << "msg buff " << output_msg_buff << endl;

        int bytes_sent = send(client_socket_descriptor, output_msg_buff, strlen(output_msg_buff), 0);
        cout << bytes_sent << endl;
        // memset (output_msg_buff, 0, MAX_OUT_BUFF_SIZE);

        if(shutdown_server) break;

    }

    close(client_socket_descriptor);
    free(client_socket);
    return NULL;

}

void* exit_handler (void* arg) {
    string input_string;
    cin >> input_string;
    if (input_string == "quit") {
        cout << "SERVER Exiting..." << endl;
    }
    shutdown_server = true;
    exit(EXIT_SUCCESS);
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

int main (int argc, const char* argv[]) {
    
    string tracker_info_file_path = string(argv[1]);
    int tracker_no = stoi(argv[2]);

    int tracker_socket_descriptor;
    // int client_socket_descriptor;
    int opt = 1;
    ssize_t bytes_read = 0;

    const int MAX_INP_BUFF_SIZE = 1024;
    const int MAX_OUT_BUFF_SIZE = 1024;
    
    char* input_msg_buff = (char*)malloc(MAX_INP_BUFF_SIZE * sizeof(char));
    char* output_msg_buff = (char*)malloc(MAX_OUT_BUFF_SIZE * sizeof(char));
    char* command = (char*)malloc(10 * sizeof(char));
    
    char* msg = "OK";
    const char space_sep[1] = {' '};

    //Port and IP Address data
    uint16_t PORT = get_port_from_file(tracker_info_file_path, tracker_no);
    const char* ip_address = get_address_from_file(tracker_info_file_path, tracker_no).c_str();
    
    struct sockaddr_in tracker_address;
    socklen_t tracker_addr_len = sizeof(tracker_address);

    if ((tracker_socket_descriptor = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("ERROR : Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(tracker_socket_descriptor, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("ERROR : Setsockopt Failed");
        exit(EXIT_FAILURE);
    }

    tracker_address.sin_family = AF_INET;  //IPv4
    tracker_address.sin_addr.s_addr = INADDR_ANY;
    tracker_address.sin_port = htons(PORT);

    if (bind(tracker_socket_descriptor, (struct sockaddr*)&tracker_address, sizeof(tracker_address)) < 0) {
        perror("ERROR : Socket bind failed");
        exit(EXIT_FAILURE);
    }

    //Listen for connection attempts by client
    if (listen(tracker_socket_descriptor, 5) < 0) {
        perror("ERROR : Listening Falied");
        exit(EXIT_FAILURE);
    }

    pthread_t exit_thread;
    if (pthread_create(&exit_thread, NULL, exit_handler, NULL) != 0) {
        perror ("ERROR : Unable to create EXIT_THREAD");
    }
    else {
        pthread_detach(exit_thread);
    }

    cout << "SERVER STARTED...";
    cout << "Listening on PORT " << PORT << "..." << endl;

    while(!shutdown_server) {
        int* new_client_socket_descriptor = (int*)malloc(1 * sizeof(int));

        //Accept incoming client requests
        if ((*new_client_socket_descriptor = accept(tracker_socket_descriptor, (struct sockaddr*)&tracker_address, (socklen_t*)&tracker_addr_len)) < 0) {
            perror("ERROR : Unable to accept connection from client");
            exit(EXIT_FAILURE);
        }
        cout << "New Client connected with id " << *new_client_socket_descriptor << endl;

        pthread_t new_client_thread;
        if (pthread_create(&new_client_thread, NULL, client_handler, (void*)new_client_socket_descriptor) != 0) {
            perror ("ERROR : Unable to create thread for new Client");
            delete new_client_socket_descriptor;
        }
        else {
            pthread_detach(new_client_thread);
        }
    }

    close(tracker_socket_descriptor); 

    return 0;

}