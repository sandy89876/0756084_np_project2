//
//  np_single_proc.cpp
//  np_project_2
//
//  Created by 胡育旋 on 2018/10/28.
//  Copyright © 2018 胡育旋. All rights reserved.
//


#include <iostream>
#include <fstream>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <algorithm>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <vector>
#include <set>
#include <map>
#include <deque>
#include <queue>

#include "utility.h"
#include "conn_utility.h"

using namespace std;

#define buffersize 15000
#define maxClientNum 30

vector<string> split_line(string input,char* delimeter);
set<string> get_known_command_set();
void set_pipe_array(int* pipe_array);
void parse_cmd(client &cur_client, vector<string> &tokens, string inputLine);//inputLine for boardcasting userpipe msg
void set_current_cmd_pipe_out(command cmd);
void execute_cmd(command cmd);
bool check_and_set_same_pipe_out(set<unhandled_pipe_obj> unhandled_pipe_obj_set, int exe_line_num, int* pipe_array);
void set_write_file_output(string fileName);

void close_connection_handler(client &cur_client);
int assign_user_id();
void broadcast_msg(string msg);
void client_handler(int cli_socketfd);
void new_client_handler();
void set_client_env(client &cur_client);
client* get_client_by_socketfd(int socket);
int get_client_socketfd_by_id(int id);
string get_client_name_by_id(int id);
bool user_name_exist(string name);
bool user_exist(int id);
bool user_pipe_exist(int sender_id, int recv_id);

int socketfd;
struct sockaddr_in serv_addr, cli_addr;

vector<client> client_list;
set<user_pipe_info, compare> user_pipe_set;
bool user_id_used[maxClientNum]={false};
fd_set all_fds;
fd_set tmp_fds; 
int fdmax;

int main(int argc, const char * argv[]){

    int port = atoi(argv[1]);

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd == -1) puts("Server : Could not create Internet stream socket");
    printf("Server: create socket\n");
    
    //Prepare the sockaddr_in structure
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    int reuse = 1;
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
            perror("setsockopt(SO_REUSEADDR) failed");
     
    #ifdef SO_REUSEPORT
        if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuse, sizeof(reuse)) < 0) 
            perror("setsockopt(SO_REUSEPORT) failed");
    #endif
    
    //Bind
    if(bind(socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        puts("Server : Could not bind local address");
        return 1;
    }
    printf("Server: bind address\n");

    //Listen
    listen(socketfd, 30);
    printf("Server: listen\n");

    FD_ZERO(&all_fds);
    FD_ZERO(&tmp_fds);
    FD_SET(socketfd,&all_fds);
    fdmax = socketfd;

    while(1){
        tmp_fds = all_fds;
        if(select(fdmax+1, &tmp_fds, NULL, NULL, NULL) == -1){
            if(errno == EINTR){
                continue;
            }else{
                perror("select");
                exit(4);
            }
        }

        for(int i = 0; i <= fdmax; i++){
            if(FD_ISSET(i, &tmp_fds)){
                if(i == socketfd){
                    //handle new connections
                    new_client_handler();
                }else{
                    //handle input from client
                    client_handler(i);
                }
            }
        }
    }
}

set<string> get_known_command_set(){

    set<string> result;
    DIR *pDIR;
    struct dirent *entry;
    const char* env_path = getenv("PATH");
    char* path;
    path = strdup(env_path);
    char* tmppath;
    tmppath = strtok(path,":");
    //cout << "known commands:";
    
    if(pDIR = opendir(tmppath)){
        while(entry = readdir(pDIR)){
            //ignore . .. .DS_store
            if( entry->d_name[0] != '.'){
                //cout << entry->d_name << " ";
                result.insert(entry->d_name);
            }
        }
        closedir(pDIR);
    }
    
    result.insert(">");
    //cout << endl;
    return result;
}

void set_write_file_output(string fileName){
    FILE *fp = fopen(fileName.c_str(),"w");
    int fp_num = fileno(fp);
    dup2(fp_num, STDOUT_FILENO);
    close(fp_num);
}

void set_current_cmd_pipe_out(command cmd){
    //cout << cmd.name << " pipe to " << cmd.pipe_arr[1] << endl;
    close(cmd.pipe_arr[0]);
    dup2(cmd.pipe_arr[1], STDOUT_FILENO);
    if(cmd.output_type == "both"){
        dup2(cmd.pipe_arr[1], STDERR_FILENO);
    }
    close(cmd.pipe_arr[1]);
}

void set_pipe_array(int* pipe_array){
    if(pipe(pipe_array) == -1){
        cout << "can't create pipe" << endl;
    }
}

void parse_cmd(client &cur_client, vector<string> &tokens, string inputLine){
    for(int i = 0; i < tokens.size(); i++){
        if(tokens[i] == ">"){
               
            //set previous job need pipe out
            command *prev_cmd = &(cur_client.current_job_queue.back());
            prev_cmd->need_pipe_out = true;

            //set previous job need to write file
            prev_cmd->is_write_file = true;
            prev_cmd->write_file_name = tokens[++i];
            
        }else if(is_stdout_numbered_pipe(tokens[i])){
            // |n insert unhandled_pipe_obj{pipe_array, exe_line_num} into unhandled_pipe_arr_set
            
            int n = stoi(tokens[i].substr(1,tokens[i].length()));
            n += cur_client.line_count;
            command *last_cmd = &(cur_client.current_job_queue.back());
            last_cmd->before_numbered_pipe = true;
            last_cmd->exe_line_num = n;
            last_cmd->need_pipe_out = true;

            if(!check_and_set_same_pipe_out(cur_client.unhandled_pipe_obj_set, n, last_cmd->pipe_arr)){
                set_pipe_array(last_cmd->pipe_arr);

                unhandled_pipe_obj new_unhandled_pipe_obj;
                new_unhandled_pipe_obj.exe_line_num = n;
                new_unhandled_pipe_obj.pipe_arr[0] = last_cmd->pipe_arr[0];
                new_unhandled_pipe_obj.pipe_arr[1] = last_cmd->pipe_arr[1];
                cur_client.unhandled_pipe_obj_set.insert(new_unhandled_pipe_obj);
            }

            //cout << "last_cmd set pipe array " << last_cmd->pipe_arr[0] << " " << last_cmd->pipe_arr[1] << endl;
            
            
        }else if(is_stderr_numbered_pipe(tokens[i])){
            // !n current_job pop_back and insert into unhandled_job(queue)
            
            int n = stoi(tokens[i].substr(1,tokens[i].length()));
            n += cur_client.line_count;
            command *last_cmd = &(cur_client.current_job_queue.back());
            last_cmd->before_numbered_pipe = true;
            last_cmd->exe_line_num = n;
            last_cmd->need_pipe_out = true;
            last_cmd->output_type = "both";

            if(!check_and_set_same_pipe_out(cur_client.unhandled_pipe_obj_set, n, last_cmd->pipe_arr)){
                set_pipe_array(last_cmd->pipe_arr);

                unhandled_pipe_obj new_unhandled_pipe_obj;
                new_unhandled_pipe_obj.exe_line_num = n;
                new_unhandled_pipe_obj.pipe_arr[0] = last_cmd->pipe_arr[0];
                new_unhandled_pipe_obj.pipe_arr[1] = last_cmd->pipe_arr[1];
                cur_client.unhandled_pipe_obj_set.insert(new_unhandled_pipe_obj);
            }

            //cout << "last_cmd set pipe array " << last_cmd->pipe_arr[0] << " " << last_cmd->pipe_arr[1] << endl;
            
        }else if(tokens[i] == "|"){
            command *last_cmd = &(cur_client.current_job_queue.back());
            last_cmd->need_pipe_out = true;
            //set_pipe_array(last_cmd->pipe_arr);
            //cout << "last_cmd set pipe array " << last_cmd->pipe_arr[0] << " " << last_cmd->pipe_arr[1] << endl;
            
        }else if(cur_client.command_set.count(tokens[i]) != 0){
            //*it is known command
            command cur_comm;
            cur_comm.name = tokens[i];
            
            //push all parameters
            vector<string> parameter;
            while((i+1) != tokens.size() && !is_ordinary_pipe(tokens[i+1]) && !is_output_to_file(tokens[i+1]) && !is_stdout_numbered_pipe(tokens[i+1]) 
                && !is_stderr_numbered_pipe(tokens[i+1]) && !is_write_to_user_pipe(tokens[i+1]) && !is_read_from_user_pipe(tokens[i+1])){
                //cout << tokens[i];
                parameter.push_back(tokens[++i]);
                if(i == tokens.size()) break;
            }
            cur_comm.arguments = parameter;
            cur_client.current_job_queue.push_back(cur_comm);
        }else if(is_write_to_user_pipe(tokens[i])){
            // >n
            int dest_user_id = stoi(tokens[i].substr(1));

            if( !user_exist(dest_user_id)){
                //receiver not exist
                string tmp = user_not_exist_msg(tokens[i].substr(1));
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
                
                //ignore this line
                cur_client.current_job_queue.clear();
                break;
            }else if(user_pipe_exist(cur_client.id, dest_user_id)){
                //pipe already exist
                string tmp = user_pipe_exist_msg(cur_client.id, dest_user_id);
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
                
                //ignore this line
                cur_client.current_job_queue.clear();
                break;
            }else{
                //create user pipe
                command *last_cmd = &(cur_client.current_job_queue.back());
                last_cmd->need_pipe_out = true;
                last_cmd->is_user_pipe_out = true;
                set_pipe_array(last_cmd->pipe_arr);

                //save user pipe information
                user_pipe_info cur_user_pipe;
                cur_user_pipe.sender_id = cur_client.id;
                cur_user_pipe.recv_id = dest_user_id;
                cur_user_pipe.pipe_arr[0] = (last_cmd->pipe_arr)[0];
                cur_user_pipe.pipe_arr[1] = (last_cmd->pipe_arr)[1];
                
                user_pipe_set.insert(cur_user_pipe);
                cout << "after insert:" << endl;
                for(set<user_pipe_info>::iterator it = user_pipe_set.begin(); it != user_pipe_set.end(); ++it){
                    cout << "sender_id= " << to_string(it->sender_id) << " recv_id= " << to_string(it->recv_id) << " ***\n";
                }

                //broadcast write to user pipe msg 
                string msg = pipe_to_user_msg(cur_client.name, cur_client.id, get_client_name_by_id(dest_user_id), dest_user_id, inputLine);
                broadcast_msg(msg);
            }
        }else if(is_read_from_user_pipe(tokens[i])){
            // <n
            int src_user_id = stoi(tokens[i].substr(1));

            if( !user_exist(src_user_id)){
                //sender not exist
                string tmp = user_not_exist_msg(tokens[i].substr(1));
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
                
                //ignore this line
                cur_client.current_job_queue.clear();
                break;
            }else if(!user_pipe_exist(src_user_id, cur_client.id)){
                //pipe not exist
                string tmp = user_pipe_not_exist_msg(src_user_id, cur_client.id);
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
                
                //ignore this line
                cur_client.current_job_queue.clear();
                break;
            }else{
                command *last_cmd = &(cur_client.current_job_queue.back());
                last_cmd->is_user_pipe_in = true;

                //set pipe, delete from user_pipe_set
                for(set<user_pipe_info>::iterator it = user_pipe_set.begin(); it != user_pipe_set.end(); ++it){
                    if(it->sender_id == src_user_id && it->recv_id == cur_client.id){
                        last_cmd->user_pipe_arr[0] = it->pipe_arr[0];
                        last_cmd->user_pipe_arr[1] = it->pipe_arr[1];
                        user_pipe_set.erase(it);
                        break;
                    }
                }

                //broadcast write to user pipe msg
                string msg = read_from_user_pipe_msg(get_client_name_by_id(src_user_id), src_user_id, cur_client.name, cur_client.id, inputLine);
                broadcast_msg(msg);
            }
        }else{
            //Unknown command
            string msg = "Unknown command: [" +tokens[i]+ "].\n";

            send(cur_client.socket_fd,msg.c_str(),msg.length(),0);
            while((i+1) != tokens.size() && (tokens[i] != "|") && (!is_output_to_file(tokens[i+1])) && (!is_stdout_numbered_pipe(tokens[i+1])) && (!is_stderr_numbered_pipe(tokens[i+1]))){
                i++;
            }
        }
    }
}

vector<string> split_line(string input,char* delimeter){
    char *comm = new char[input.length()+1];
    strcpy(comm, input.c_str());
    
    char* token = strtok(comm, delimeter);
    vector<string> result;
    while(token != NULL){
        result.push_back(token);
        token = strtok(NULL, delimeter);
    }
    return result;
}

void execute_cmd(command cmd){
    //change cmd info to execvp required data type
    vector<char*>  vc_char;
    char *name = new char[cmd.name.length()+1];
    strcpy(name,cmd.name.c_str());
    vc_char.push_back(name);
    if(cmd.arguments.size() > 0){
        transform(cmd.arguments.begin(), cmd.arguments.end(), back_inserter(vc_char), convert);  
    }
    vc_char.push_back(NULL);
    char **arg = &vc_char[0];
    
    if(execvp(arg[0], arg) < 0)puts("cmdChild : exec failed");
}

bool check_and_set_same_pipe_out(set<unhandled_pipe_obj> unhandled_pipe_obj_set, int exe_line_num, int* pipe_array){
    //if new_unhandled_pipe_obj_set has same exe_line_num as this cmd, set this cmd's pipe_array
    for (set<unhandled_pipe_obj>::iterator it=unhandled_pipe_obj_set.begin(); it!=unhandled_pipe_obj_set.end(); ++it){
        if(it->exe_line_num == exe_line_num){
            pipe_array[0] = it->pipe_arr[0];
            pipe_array[1] = it->pipe_arr[1];
            return true;
        }
    }
    return false;
}


int assign_user_id(){
    for(int i = 0; i < 30; i++){
        if(user_id_used[i] == false){
            user_id_used[i] = true;
            return i+1;
        }
    }
}

int get_client_socketfd_by_id(int id){
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->id == id){
            return it->socket_fd;
        }
    }
}

string get_client_name_by_id(int id){
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->id == id){
            return it->name;
        }
    }
}

client* get_client_by_socketfd(int socket){
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->socket_fd == socket){
            return &(*it);
        }
    }
}

bool user_name_exist(string name){
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->name == name){
            return true;
        }
    }
    return false;
}

bool user_exist(int id){
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->id == id){
            return true;
        }
    }
    return false;
}

bool user_pipe_exist(int sender_id, int recv_id){
    for(set<user_pipe_info>::iterator it = user_pipe_set.begin(); it != user_pipe_set.end(); ++it){
        // cout << "$$$$$$$check user_pipe_exist sender_id= " << to_string(it->sender_id) << "recv_id= " << to_string(it->recv_id) << "$$$$$$$\n";
        if(it->sender_id == sender_id && it->recv_id == recv_id){
            return true;
        }
    }
    return false;
}

void set_client_env(client &cur_client){
    map<string, string> env_setting = cur_client.env_setting;
    for(map<string, string>::iterator it = env_setting.begin(); it != env_setting.end(); ++it){
        cout << "set client id= " << to_string(cur_client.id) << " env " << (it->first) << "to " << (it->second) << endl;
        setenv((it->first).c_str(), (it->second).c_str(), 1);

        char* tmp = getenv((it->first).c_str());
        char tmp_buf[500];
        sprintf(tmp_buf, "%s\n", tmp);
        // cout << "getenv: " << tmp_buf <<endl;
    }
    cur_client.command_set = get_known_command_set();
}

void broadcast_msg(string msg){
    //broadcast msg
    char* tmp = strdup(msg.c_str());
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        send(it->socket_fd,tmp,strlen(tmp),0);
    }
}

void close_connection_handler(client &cur_client){
    //broadcast user leave message
    broadcast_msg(logout_msg(cur_client.name));
    user_id_used[cur_client.id-1] = false;

    //close related user_pipe and delete from user_pipe_set
    for(set<user_pipe_info>::iterator it = user_pipe_set.begin(); it != user_pipe_set.end(); ++it){
        if(it->sender_id == cur_client.id || it->recv_id == cur_client.id){
            close(it->pipe_arr[0]);
            close(it->pipe_arr[1]);

            cout << "close_handler cur_client name = " << cur_client.name <<endl;
            cout << "erase user_pipe_set sender_id= " << to_string(it->sender_id) << " recv_id= " << to_string(it->recv_id) << endl;
            user_pipe_set.erase(it);
        }
    }

    close(cur_client.socket_fd);
    //delete fd from all_fds
    FD_CLR(cur_client.socket_fd, &all_fds);

    //delete client from client_list
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        if(it->socket_fd == cur_client.socket_fd){
            client_list.erase(it);
            break;
        }
    }
    cur_client.current_job_queue.clear();
}

void new_client_handler(){
    int clilen = sizeof(cli_addr);
    int cli_socketfd = accept(socketfd, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen);
    if (cli_socketfd < 0){
        puts("Server : Accept failed");
        return;
    }

    FD_SET(cli_socketfd, &all_fds);
    if(cli_socketfd > fdmax){
        fdmax = cli_socketfd;
    }

    //send welcome message
    send(cli_socketfd,welcome_msg.c_str(),welcome_msg.length(),0);

    client new_client;
    // new_client.ip = concat_ip(inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    new_client.socket_fd = cli_socketfd;
    new_client.id = assign_user_id();
    cout << "new_client ip = " << new_client.ip << endl;
    client_list.push_back(new_client);

    broadcast_msg(login_msg(new_client.name, new_client.ip));
    send(cli_socketfd,"% ",2,0);
}

void client_handler(int cli_socketfd){
    string inputLine;
    set<int> p_id_table;

    //int cli_socketfd = cur_client.socket_fd;
    client *cur_client = get_client_by_socketfd(cli_socketfd);
    if(cur_client != NULL){
        //handle connection between child

        //change env to client's env_setting
        set_client_env(*cur_client);

        char buf[buffersize];
        int n;
        if((n = recv(cli_socketfd,buf,sizeof(buf),0)) > 0){
            inputLine = buf;
            if(inputLine.find("\n") != string::npos){
                inputLine = inputLine.substr(0,inputLine.find("\n"));
            }
            if(inputLine != "\r"){
                cur_client->line_count++;
            }
            if(inputLine.find("\r") != string::npos){
                int tmp = inputLine.find("\r");
                inputLine = inputLine.substr(0,tmp);
            }
            cout << "inputLine:"<<inputLine<<"..."<<endl;
            if(inputLine.compare("exit") == 0){
                close_connection_handler(*cur_client);
                return;
            }
            
            vector<string> tokens = split_line(inputLine, " ");
            
            if(inputLine.find("setenv") == 0){
                map<string, string>::iterator it = cur_client->env_setting.find(tokens[1]);
                if(it != cur_client->env_setting.end()){
                    //already has same key, change value
                   it->second = tokens[2];
                }else{
                    //add new environment variable to client
                    cur_client->env_setting.insert(pair<string, string>(tokens[1], tokens[2]));
                }
            }else if(inputLine.find("printenv") == 0){
                char* tmp = getenv(tokens[1].c_str());
                char tmp_buf[500];
                sprintf(tmp_buf, "%s\n", tmp);
                send(cli_socketfd,tmp_buf,strlen(tmp_buf),0);
            }else if(inputLine.find("name") == 0){
                if(user_name_exist(tokens[1])){
                    string tmp = name_exist_msg(tokens[1]);
                    send(cli_socketfd, tmp.c_str(), tmp.length(), 0);
                }else{
                    cur_client->name = tokens[1];
                    broadcast_msg(change_name_msg(cur_client->name, cur_client->ip));
                }
            }else if(inputLine.find("yell") == 0){
                string yell_content = inputLine.substr(inputLine.find("yell")+5);
                broadcast_msg(yell_msg(cur_client->name, yell_content));
            }else if(inputLine.find("tell") == 0){
                if(user_exist(stoi(tokens[1]))){
                    
                    string content = inputLine.substr(inputLine.find(tokens[1])+2);
                    string msg = tell_msg(cur_client->name, content);
                    send(get_client_socketfd_by_id(stoi(tokens[1])), msg.c_str(), msg.length(), 0);
                }else{
                    //send user not exist error msg
                    string tmp = user_not_exist_msg(tokens[1]);
                    send(cur_client->socket_fd, tmp.c_str(), tmp.length(), 0);
                }
            }else if(inputLine.compare("who") == 0){
                string info = user_info_msg(client_list, cur_client->id);
                send(cli_socketfd, info.c_str(), info.length(), 0);
            }else{
                parse_cmd(*cur_client, tokens, inputLine);
            }
            
            for(deque<command>::iterator it = cur_client->current_job_queue.begin(); it != cur_client->current_job_queue.end(); it++){
                //if this command hasn't create pipe before,create one
                if(!it->before_numbered_pipe && !it->is_write_file && !it->is_user_pipe_out){
                    set_pipe_array(it->pipe_arr);
                }

                signal(SIGCHLD, childHandler);
                
                int p_id;
                while((p_id = fork()) < 0) usleep(1000);
                if(p_id == 0){
                    dup2(cur_client->socket_fd, STDERR_FILENO);

                    //child process
                    if(it == cur_client->current_job_queue.begin()){
                        // this is first job
                        // check if unhandled_pipe_obj_set has same exe_line_num
                        for (set<unhandled_pipe_obj>::iterator it = cur_client->unhandled_pipe_obj_set.begin(); it != cur_client->unhandled_pipe_obj_set.end(); ++it){
                            if(it->exe_line_num == cur_client->line_count){
                                //cout << "child dup "<< it->pipe_arr[0] << " to STDIN"<<endl;
                                close(it->pipe_arr[1]);
                                dup2(it->pipe_arr[0], STDIN_FILENO);
                                close(it->pipe_arr[0]);
                            }
                        }
                    }else{
                        //this is not the first job, check previous cmd
                        //get data from previous cmd
                        if((it-1)->need_pipe_out){
                            //cout << "child dup "<<(it-1)->pipe_arr[0] << " to STDIN" << endl;
                            close((it-1)->pipe_arr[1]);
                            dup2((it-1)->pipe_arr[0], STDIN_FILENO);
                            close((it-1)->pipe_arr[0]);
                        }
                    }

                    if(it->is_write_file){
                        set_write_file_output(it->write_file_name);
                    }else{
                        if(it->need_pipe_out){
                            set_current_cmd_pipe_out(*it);
                        }else{
                            dup2(cli_socketfd,STDOUT_FILENO);
                        }
                    }
                    if(it->is_user_pipe_in){
                        close(it->user_pipe_arr[1]);
                        dup2(it->user_pipe_arr[0], STDIN_FILENO);
                        close(it->user_pipe_arr[0]);
                    }
                    execute_cmd(*it);
                    exit(1);
                }else{
                    //add p_id into table
                    p_id_table.insert(p_id);
                                       
                    //if need input pipe and it has been opened, close it.
                    if(it != cur_client->current_job_queue.begin() && (it-1)->need_pipe_out){
                        close((it-1)->pipe_arr[0]);
                        close((it-1)->pipe_arr[1]);
                    }

                    //close unhandled pipe
                    for (set<unhandled_pipe_obj>::iterator unhand_it = cur_client->unhandled_pipe_obj_set.begin(); unhand_it != cur_client->unhandled_pipe_obj_set.end(); ++unhand_it){
                        if(unhand_it->exe_line_num == cur_client->line_count){
                            close(unhand_it->pipe_arr[1]);
                            close(unhand_it->pipe_arr[0]);
                        }
                    }

                    //execute finish
                    if(!it->need_pipe_out ){
                        //if this job doesn't need to pipe out,close pipe
                        close(it->pipe_arr[0]);
                        close(it->pipe_arr[1]);
                    }
                    
                    //close previous job pipe
                    if(it != cur_client->current_job_queue.begin()){
                        command prev_cmd = *(it-1);
                        close((it-1)->pipe_arr[0]);
                        close((it-1)->pipe_arr[1]);
                    }

                    //close user pipe
                    if(it->is_user_pipe_in){
                        close(it->user_pipe_arr[0]);
                        close(it->user_pipe_arr[1]);
                    }
                }

                if((it+1) == cur_client->current_job_queue.end() && !cur_client->current_job_queue.back().before_numbered_pipe){
                    for(set<int>::iterator s_it = p_id_table.begin(); s_it != p_id_table.end(); ++s_it){
                        
                        int status;
                        waitpid(*s_it, &status, 0);

                        //wait完 delete from p_id_table
                        p_id_table.erase(*s_it);
                    }
                }
            }
            
            cur_client->current_job_queue.clear();
            memset(buf, 0, sizeof(buf));
            send(cli_socketfd,"% ",2,0);
        }else{
            close_connection_handler(*cur_client);
        }
    }else{
        cout << "client_handler client NULL ???" << endl;
    }  
}