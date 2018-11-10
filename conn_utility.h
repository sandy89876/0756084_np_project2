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

bool comp_client_id(client& x, client& y){
        return x.id < y.id;
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
    //*** User from 140.113.215.63/1013 is named ’Banana’. ***
    string tmp = stars + " User from " + ip + " is named '" + name + "'. " + stars + "\n";
    return tmp;
}

string yell_msg(string name, string content){
    string tmp = stars + " " + name + " yelled " + stars + ": " + content + "\n";
    return tmp;
}

string user_info_msg(vector<client> &client_list, int id){
    string tmp = "<ID> <nickname>    <IP/port>    <indicate me>\n";
    sort(client_list.begin(), client_list.end(), comp_client_id);
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->id == id){
            tmp.append(to_string(id)+ "    " + it->name + "     " + it->ip + "    <-me\n");
        }else{
            tmp.append(to_string(id)+ "    " + it->name + "    " + it->ip + "\n");
        }
    }
    return tmp;
}

string pipe_to_user_msg(string sender_name, int sender_id, string recv_name, int recv_id, command cmd){
    //string tmp = stars + " " + sender_name + " (#" + sender_id + ") just piped '" + cmd.name + 
}