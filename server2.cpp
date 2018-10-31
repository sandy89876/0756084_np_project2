//
//  server1.cpp
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
#include <deque>
#include <queue>

#include "utility.h"

using namespace std;

#define buffersize 15000

struct command{
    string name;
    vector<string> arguments;
    bool before_numbered_pipe = false;
    bool need_pipe_out = false;
    int pipe_arr[2]={0,1};
    int exe_line_num = 0;
    string output_type="stdout";//stdout or both(stdout.stderr)
    bool is_write_file = false;
    string write_file_name;
};

struct unhandled_pipe_obj{
    int exe_line_num;
    int pipe_arr[2];
};

bool operator <(const unhandled_pipe_obj& x, const unhandled_pipe_obj& y){
        return x.exe_line_num < y.exe_line_num;
};

struct client{
    int id;
    string name = "(no name)";
    string ip;
    int socket_fd;
};

vector<string> split_line(string input,char* delimeter);
void initial_setting();
set<string> get_known_command_set();
void set_pipe_array(int* pipe_array);
void parse_cmd(int fd, int count);
void set_current_cmd_pipe_out(command cmd);
void execute_cmd(command cmd);
bool check_and_set_same_pipe_out(int exe_line_num,int* pipe_array);
void set_write_file_output(string fileName);

void close_connection_handler(int fd);
int assign_user_id();
void broadcast_msg(string msg);
void user_login_handler(struct sockaddr_in cli_addr, int cli_socketfd);

//TODO:move cmd_set, cur_job_que, unhan_pip_obj_set, p_id_table to client
set<string> command_set;
deque<command> current_job_queue;
vector<string> tokens;//client??
set<unhandled_pipe_obj> unhandled_pipe_obj_set;
set<int> p_id_table;


vector<client> client_list;
bool user_id_used[30]={false};

int main(int argc, const char * argv[]){
    int socketfd, cli_socketfd;
    struct sockaddr_in serv_addr, cli_addr;

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd == -1) puts("Server : Could not create Internet stream socket");
    printf("Server: create socket\n");
    
    //Prepare the sockaddr_in structure
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9999);
    
    //Bind
    if(bind(socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        puts("Server : Could not bind local address");
        return 1;
    }
    printf("Server: bind address\n");

    //Listen
    listen(socketfd, 30);
    printf("Server: listen\n");

    while(1){
        //Accept connection from an incoming client
        int clilen = sizeof(cli_addr);
        cli_socketfd = accept(socketfd, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen);
        if (cli_socketfd < 0){
            puts("Server : Accept failed");
            return 1;
        }

        //send welcome message
        send(cli_socketfd,welcome_msg.c_str(),welcome_msg.length(),0);
        user_login_handler(cli_addr, cli_socketfd);

        /*
        int cli_pid = fork();

        if(cli_pid < 0){
            cout << "cannot fork cild process for new connection" << endl;
        }else if(cli_pid == 0){
            //handle client action

        }else{

        }
        */
        
        string inputLine;

        initial_setting();
        //handle connection between child
        int line_count = 0;

        dup2(cli_socketfd,STDOUT_FILENO);
        //cout << "% ";
        send(cli_socketfd,"% ",3,0);
        char buf[buffersize];
        int n;
        while((n = recv(cli_socketfd,buf,sizeof(buf),0)) != 0){
            inputLine = buf;
            inputLine = inputLine.substr(0,inputLine.find("\n")-1);
            //cout << "inputLine:"<<inputLine<<"..."<<endl;

            //count line for |n
            if(inputLine != "\0"){
                line_count++;
            }
            //cout << "line:" <<line_count<<endl;
            if(inputLine.compare("exit") == 0){
                close_connection_handler(cli_socketfd);
                break;
            }
            
            tokens = split_line(inputLine, " ");
            
            if(inputLine.find("setenv") != string::npos){
                setenv(tokens[1].c_str(), tokens[2].c_str(), 1);
                command_set = get_known_command_set();
                
            }else if(inputLine.find("printenv") != string::npos){
                char* tmp = getenv(tokens[1].c_str());
                tmp[strlen(tmp)] = '\n';
                tmp[strlen(tmp)] = '\0';
                send(cli_socketfd,tmp,strlen(tmp)+1,0);
            }else{
                parse_cmd(cli_socketfd, line_count);
            }
            
            for(deque<command>::iterator it = current_job_queue.begin(); it != current_job_queue.end(); it++){
                //if this command hasn't create pipe before,create one
                if(!it->before_numbered_pipe && !it->is_write_file){
                    set_pipe_array(it->pipe_arr);
                }

                signal(SIGCHLD, childHandler);

                int p_id;
                while((p_id = fork()) < 0) usleep(1000);
                if(p_id == 0){

                    //child process
                    if(it == current_job_queue.begin()){
                        // this is first job
                        // check if unhandled_pipe_obj_set has same exe_line_num
                        for (set<unhandled_pipe_obj>::iterator it=unhandled_pipe_obj_set.begin(); it!=unhandled_pipe_obj_set.end(); ++it){
                            if(it->exe_line_num == line_count){
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
                        }
                    }
                    execute_cmd(*it);

                    exit(1);
                }else{
                    //add p_id into table
                    p_id_table.insert(p_id);
                    
                    
                    //if need input pipe and it has been opened, close it.
                    if(it != current_job_queue.begin() && (it-1)->need_pipe_out){
                        //cout << "parent close " << (it-1)->pipe_arr[0] << " and " << (it-1)->pipe_arr[1] << endl;
                        close((it-1)->pipe_arr[0]);
                        close((it-1)->pipe_arr[1]);
                    }

                    
                    //close unhandled pipe
                    for (set<unhandled_pipe_obj>::iterator unhand_it=unhandled_pipe_obj_set.begin(); unhand_it!=unhandled_pipe_obj_set.end(); ++unhand_it){
                        if(unhand_it->exe_line_num == line_count){
                            //cout << "parent close "<< it->pipe_arr[0] << " and " << it->pipe_arr[1] <<endl;
                            close(unhand_it->pipe_arr[1]);
                            close(unhand_it->pipe_arr[0]);
                        }
                    }

                    //execute finish
                    if(!it->need_pipe_out ){
                        //if this job doesn't need to pipe out,close pipe
                        //cout << "parent close " << it->pipe_arr[0] << " and " << it->pipe_arr[1] << endl;
                        close(it->pipe_arr[0]);
                        close(it->pipe_arr[1]);
                        //cout << "close this job pipe" << endl;
                    }
                    
                    //close previous job pipe
                    if(it != current_job_queue.begin()){
                        
                        command prev_cmd = *(it-1);
                        close((it-1)->pipe_arr[0]);
                        close((it-1)->pipe_arr[1]);
                        //cout << "close previous job pipe" << endl;
                    }
                    
                }

                if((it+1) == current_job_queue.end() && !current_job_queue.back().before_numbered_pipe){
                    for(set<int>::iterator s_it = p_id_table.begin(); s_it != p_id_table.end(); ++s_it){
                        
                        int status;
                        waitpid(*s_it, &status, 0);

                        //wait完 delete from p_id_table
                        p_id_table.erase(*s_it);

                    }
                }

            }
            
            current_job_queue.clear();

            send(cli_socketfd,"% ",3,0);
        }
        close(cli_socketfd);
    }
    close(socketfd);
}


void initial_setting(){
    setenv("PATH", "bin:.", 1);
    command_set = get_known_command_set();
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

void parse_cmd(int fd, int count){
    for(int i = 0; i < tokens.size(); i++){
        if(tokens[i] == ">"){
               
            //set previous job need pipe out
            command *prev_cmd = &current_job_queue.back();
            prev_cmd->need_pipe_out = true;

            //set previous job need to write file
            prev_cmd->is_write_file = true;
            prev_cmd->write_file_name = tokens[++i];
            
        }else if(is_stdout_numbered_pipe(tokens[i])){
            // |n insert unhandled_pipe_obj{pipe_array, exe_line_num} into unhandled_pipe_arr_set
            
            int n = stoi(tokens[i].substr(1,tokens[i].length()));
            n += count;
            command *last_cmd = &current_job_queue.back();
            last_cmd->before_numbered_pipe = true;
            last_cmd->exe_line_num = n;
            last_cmd->need_pipe_out = true;

            if(!check_and_set_same_pipe_out(n, last_cmd->pipe_arr)){
                set_pipe_array(last_cmd->pipe_arr);

                unhandled_pipe_obj new_unhandled_pipe_obj;
                new_unhandled_pipe_obj.exe_line_num = n;
                new_unhandled_pipe_obj.pipe_arr[0] = last_cmd->pipe_arr[0];
                new_unhandled_pipe_obj.pipe_arr[1] = last_cmd->pipe_arr[1];
                unhandled_pipe_obj_set.insert(new_unhandled_pipe_obj);
            }

            //cout << "last_cmd set pipe array " << last_cmd->pipe_arr[0] << " " << last_cmd->pipe_arr[1] << endl;
            
            
        }else if(is_stderr_numbered_pipe(tokens[i])){
            // !n current_job pop_back and insert into unhandled_job(queue)
            
            int n = stoi(tokens[i].substr(1,tokens[i].length()));
            n += count;
            command *last_cmd = &current_job_queue.back();
            last_cmd->before_numbered_pipe = true;
            last_cmd->exe_line_num = n;
            last_cmd->need_pipe_out = true;
            last_cmd->output_type = "both";

            if(!check_and_set_same_pipe_out(n, last_cmd->pipe_arr)){
                set_pipe_array(last_cmd->pipe_arr);

                unhandled_pipe_obj new_unhandled_pipe_obj;
                new_unhandled_pipe_obj.exe_line_num = n;
                new_unhandled_pipe_obj.pipe_arr[0] = last_cmd->pipe_arr[0];
                new_unhandled_pipe_obj.pipe_arr[1] = last_cmd->pipe_arr[1];
                unhandled_pipe_obj_set.insert(new_unhandled_pipe_obj);
            }

            //cout << "last_cmd set pipe array " << last_cmd->pipe_arr[0] << " " << last_cmd->pipe_arr[1] << endl;
            
        }else if(tokens[i] == "|"){
            command *last_cmd = &current_job_queue.back();
            last_cmd->need_pipe_out = true;
            //set_pipe_array(last_cmd->pipe_arr);
            //cout << "last_cmd set pipe array " << last_cmd->pipe_arr[0] << " " << last_cmd->pipe_arr[1] << endl;
            
        }else if(command_set.count(tokens[i]) != 0){
            //*it is known command
            command cur_comm;
            cur_comm.name = tokens[i];
            
            //push all parameters
            vector<string> parameter;
            while((i+1) != tokens.size() && !is_ordinary_pipe(tokens[i+1]) && !is_output_to_file(tokens[i+1]) && !is_stdout_numbered_pipe(tokens[i+1]) && !is_stderr_numbered_pipe(tokens[i+1])){
                //cout << tokens[i];
                parameter.push_back(tokens[++i]);
                if(i == tokens.size()) break;
            }
            cur_comm.arguments = parameter;
            current_job_queue.push_back(cur_comm);
        }else{
            //Unknown command
            string msg = "Unknown command: [" +tokens[i]+ "].\n";

            send(fd,msg.c_str(),msg.length(),0);
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

bool check_and_set_same_pipe_out(int exe_line_num,int* pipe_array){
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

void close_connection_handler(int fd){
    close(fd);
    //TODO: delete client from client_list
    current_job_queue.clear();
}

void user_login_handler(struct sockaddr_in cli_addr, int cli_socketfd){
    client new_client;
    new_client.ip = concat_ip(inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    new_client.socket_fd = cli_socketfd;
    new_client.id = assign_user_id();
    cout << "new_client ip = " << new_client.ip << endl;
    client_list.push_back(new_client);

    broadcast_msg(login_msg(new_client.name, new_client.ip));
}

int assign_user_id(){
    for(int i = 0; i < 30; i++){
        if(user_id_used[i] == false){
            user_id_used[i] = true;
            return i;
        }
    }
}

void broadcast_msg(string msg){
    //broadcast msg
    char* tmp = strdup(msg.c_str());
    for(vector<client>::iterator it = client_list.begin(); it != client_list.end(); ++it){
        send(it->socket_fd,tmp,strlen(tmp),0);
    }
}