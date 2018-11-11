struct client{
    int id;
    string name = "(no name)";
    string ip;
    int socket_fd;
    set<string> command_set;
    deque<command> current_job_queue;
    set<unhandled_pipe_obj> unhandled_pipe_obj_set;
    int line_count = 0;
    map<string, string> env_setting = {{"PATH","bin:."}};
};

bool cmp_client(const client& x, const client& y){
    return x.id < y.id;
};

struct user_pipe_info{
    int sender_id;
    int recv_id;
    int pipe_arr[2];
};

bool operator <(const user_pipe_info& x, const user_pipe_info& y){
    return x.sender_id < y.sender_id;
};

string welcome_msg = "***************************************\n"\
"** Welcome to the information server **\n"\
"***************************************\n";

string stars = "***";

string concat_ip(string ip, uint16_t port){
    string tmp = ip.append("/");
    tmp += to_string(port);
    return tmp;
}

string login_msg(string name, string ip){
    string tmp = stars + " User '"+ name +\
         "' entered from " + ip + ". " + stars + "\n";
    return tmp;
}

string logout_msg(string name){
    string tmp = stars + " User '"+ name + "' left. " + stars + "\n";
    return tmp;
}

string change_name_msg(string name, string ip){
    string tmp = stars + " User from " + ip + " is named '" + name + "'. " + stars + "\n";
    return tmp;
}

string name_exist_msg(string name){
    string tmp = stars + " User '" + name + "' already exists. " + stars + "\n";
    return tmp;
}

string yell_msg(string name, string content){
    string tmp = stars + " " + name + " yelled " + stars + ": " + content + "\n";
    return tmp;
}

string tell_msg(string name, string content){
    string tmp = stars + " " + name + " told you " + stars + ": " + content + "\n";
    return tmp;
}

string user_info_msg(vector<client> &client_list, int id){
    string tmp = "<ID> <nickname>    <IP/port>    <indicate me>\n";
    
    sort(client_list.begin(), client_list.end(), cmp_client);
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->id == id){
            tmp.append(to_string(it->id)+ "    " + it->name + "     " + it->ip + "    <-me\n");
        }else{
            tmp.append(to_string(it->id)+ "    " + it->name + "    " + it->ip + "\n");
        }
    }
    return tmp;
}

string user_not_exist_msg(string id){
    string tmp = stars + " Error: user #" + id + " does not exist yet. " + stars + "\n";
    return tmp;
}

string user_pipe_exist_msg(int sender_id, int recv_id){
    string tmp = stars + " Error: the pipe #" + to_string(sender_id) + "->#" + to_string(recv_id) + " already exists. " + stars + "\n";
    return tmp;
}

string user_pipe_not_exist_msg(int sender_id, int recv_id){
    string tmp = stars + " Error: the pipe #" + to_string(sender_id) + "->#" + to_string(recv_id) + " does not exist yet. " + stars + "\n";
    return tmp;
}

string pipe_to_user_msg(string sender_name, int sender_id, string recv_name, int recv_id, string content){
    string tmp = stars + " " + sender_name + " (#" + to_string(sender_id) + ") just piped '" + content + "' to " + recv_name + " (#" + to_string(recv_id) + ") " + stars + "\n";
    return tmp;
}

string read_from_user_pipe(string sender_name, int sender_id, string recv_name, int recv_id, string content){
    string tmp = stars + " " + recv_name + " (#" + to_string(recv_id) + ") just received from " + sender_name + " (#" + to_string(sender_id) + ") by '" + content + "' " + stars + "\n";
    return tmp;
}