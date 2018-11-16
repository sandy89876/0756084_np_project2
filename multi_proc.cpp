//
//  multi_proc.cpp
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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cstdio>
#include <cerrno>

#include <vector>
#include <set>
#include <deque>
#include <queue>
#include <map>

#include "utility.h"
#include "conn_utility.h"
#include "utility_shm.h"

using namespace std;

vector<string> split_line(string input,char* delimeter);
void initial_setting();
set<string> get_known_command_set();
void set_pipe_array(int* pipe_array);
void parse_cmd(int fd, int count, vector<string> &tokens, client_shm &cur_client, string inputLine);
void set_current_cmd_pipe_out(command cmd);
void execute_cmd(command cmd);
bool check_and_set_same_pipe_out(int exe_line_num,int* pipe_array);
void set_write_file_output(string fileName);
void set_read_file(string fileName);

void close_connection_handler(client_shm &cur_client);
int assign_user_id();
int assign_user_pipe_id();
bool user_name_exist(string name);
bool user_exist(int id);
bool user_pipe_exist(int sender_id, int recv_id);
void broadcast_msg(string msg, client_shm *shmclientTable);
string get_client_name_by_id(int id);
client_shm* new_client_handler(int cli_socketfd, struct sockaddr_in &cli_addr, client_shm *shmclientTable);
void client_handler(client_shm &cur_client, client_shm *shmclientTable);

void create_shm_tables();
void attach_shm_tables();
void detach_shm_tables();
void sig_handler(int signo, siginfo_t *info, void *context);

set<string> command_set;
deque<command> current_job_queue;
set<unhandled_pipe_obj> unhandled_pipe_obj_set;
set<int> p_id_table;

int socketfd, cli_socketfd;
struct sockaddr_in serv_addr, cli_addr;
int clilen;

//var for shared client list 
client_shm *shmclientTable;
int shmclientTableid;
//var for shared buffer
char *msgbuffer;
int msgBufferid;
//var for shared used_user_id list
int *shmidTable;
int shmidTableid;

//var for shared used_user_id list
int *shmUPidTable;
int shmUPidTableid;
user_pipe_info *shmUserPipeTable;
int shmUserPipeTableid;

int main(int argc, const char * argv[]){

    int port = atoi(argv[1]);
    int child_p_id;
    
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
    listen(socketfd, 5);
    printf("Server: listen\n");

    create_shm_tables();

    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sig_handler;
    signal (SIGCHLD, SIG_IGN);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);


    while(1){
        //Accept connection from an incoming client
        clilen = sizeof(cli_addr);
        cli_socketfd = accept(socketfd, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen);
        if (cli_socketfd < 0){
            if (errno == EINTR)
                continue;
            puts("Server : Accept failed");
            return 1;
        }

        child_p_id = fork();
        if(child_p_id < 0){
            cout << "fork client process error." << endl;
        }else if(child_p_id == 0){
            close(socketfd);
            attach_shm_tables();

            client_shm *cur_client = new_client_handler(cli_socketfd, cli_addr, shmclientTable);
            //handle connection between child
            client_handler(*cur_client, shmclientTable);
            detach_shm_tables();
            
            exit(0);

        }else{
            //parent
            // close(socketfd);
            close(cli_socketfd);

        }
    }
    close(socketfd);
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
    
    if(pDIR = opendir(tmppath)){
        while(entry = readdir(pDIR)){
            //ignore . .. .DS_store
            if( entry->d_name[0] != '.'){
                result.insert(entry->d_name);
            }
        }
        closedir(pDIR);
    }
    result.insert(">");
    return result;
}

void set_write_file_output(string fileName){
    FILE *fp = fopen(fileName.c_str(),"w");
    int fp_num = fileno(fp);
    dup2(fp_num, STDOUT_FILENO);
    close(fp_num);
}

void set_read_file(string fileName){
    FILE *fp = fopen(fileName.c_str(),"r");
    int fp_num = fileno(fp);
    dup2(fp_num, STDIN_FILENO);
    close(fp_num);
}

void set_current_cmd_pipe_out(command cmd){
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

void parse_cmd(int fd, int count, vector<string> &tokens, client_shm &cur_client, string inputLine){
    int has_up_in = -1;
    int has_up_out = -1;
    string broad_msg;

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
    
        }else if(command_set.count(tokens[i]) != 0){
            //*it is known command
            command cur_comm;
            cur_comm.name = tokens[i];
            
            //push all parameters
            vector<string> parameter;
            while((i+1) != tokens.size() && !is_ordinary_pipe(tokens[i+1]) && !is_output_to_file(tokens[i+1]) && !is_stdout_numbered_pipe(tokens[i+1]) 
                && !is_stderr_numbered_pipe(tokens[i+1]) && !is_write_to_user_pipe(tokens[i+1]) && !is_read_from_user_pipe(tokens[i+1])){
                parameter.push_back(tokens[++i]);
                if(i == tokens.size()) break;
            }
            cur_comm.arguments = parameter;
            current_job_queue.push_back(cur_comm);
        }else if(is_write_to_user_pipe(tokens[i])){
            // >n
            int dest_user_id = stoi(tokens[i].substr(1));

            if( !user_exist(dest_user_id)){
                //receiver not exist
                string tmp = user_not_exist_msg(tokens[i].substr(1));
                strcpy(msgbuffer, tmp.c_str());
                kill(shmclientTable[cur_client.id-1].p_id, SIGUSR1);

                //ignore this line
                current_job_queue.clear();
                break;
            }else if(user_pipe_exist(cur_client.id, dest_user_id)){
                //pipe already exist
                string tmp = user_pipe_exist_msg(cur_client.id, dest_user_id);
                strcpy(msgbuffer, tmp.c_str());
                kill(shmclientTable[cur_client.id-1].p_id, SIGUSR1);

                //ignore this line
                current_job_queue.clear();
                break;
            }else{
                //create user pipe
                command *last_cmd = &(current_job_queue.back());
                last_cmd->is_user_pipe_out = true;
                last_cmd->user_pipe_dest_id = dest_user_id;

                //save user pipe information
                user_pipe_info cur_user_pipe;
                cur_user_pipe.sender_id = cur_client.id;
                cur_user_pipe.recv_id = dest_user_id;
                shmUserPipeTable[assign_user_pipe_id()] = cur_user_pipe;

                has_up_out = dest_user_id;
            }
        }else if(is_read_from_user_pipe(tokens[i])){
            // <n
            int src_user_id = stoi(tokens[i].substr(1));

            if( !user_exist(src_user_id)){
                //sender not exist
                string tmp = user_not_exist_msg(tokens[i].substr(1));
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
                
                //ignore this line
                current_job_queue.clear();
                break;
            }else if(!user_pipe_exist(src_user_id, cur_client.id)){
                //pipe not exist
                string tmp = user_pipe_not_exist_msg(src_user_id, cur_client.id);
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
                
                //ignore this line
                current_job_queue.clear();
                break;
            }else{
                command *last_cmd = &(current_job_queue.back());
                last_cmd->is_user_pipe_in = true;
                last_cmd->user_pipe_src_id = src_user_id;
                has_up_in = src_user_id;
                for(int i=0; i<900; i++){
                    if(shmUPidTable[i] == 1 && shmUserPipeTable[i].sender_id == src_user_id && shmUserPipeTable[i].recv_id == cur_client.id){
                        shmUPidTable[i] = 0;
                        break;
                    }
                }

            }
        }else{
            //Unknown command
            string msg = "Unknown command: [" +tokens[i]+ "].\n";

            send(fd,msg.c_str(),msg.length(),0);
            while((i+1) != tokens.size() && (tokens[i] != "|") && (!is_output_to_file(tokens[i+1])) && (!is_stdout_numbered_pipe(tokens[i+1])) && (!is_stderr_numbered_pipe(tokens[i+1]))){
                i++;
            }
        }
    }

    //broadcast msg order: recv > send
    if(has_up_in != -1){
        broad_msg.append(read_from_user_pipe_msg(get_client_name_by_id(has_up_in), has_up_in, cur_client.name, cur_client.id, inputLine));
    }
    if(has_up_out != -1){
        broad_msg.append(pipe_to_user_msg(cur_client.name, cur_client.id, get_client_name_by_id(has_up_out), has_up_out, inputLine));
    }

    if(has_up_in != -1 || has_up_out != -1){
        broadcast_msg(broad_msg, shmclientTable);
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

int assign_user_id(){
    for(int i = 0; i < 30; i++){
        if(shmidTable[i] == 0){
            shmidTable[i] = 1;
            return i;
        }
    }
}

int assign_user_pipe_id(){
    for(int i = 0; i < 900; i++){
        if(shmUPidTable[i] == 0){
            shmUPidTable[i] = 1;
            return i;
        }
    }
}

void create_shm_tables(){
    //create shmidTable
    shmidTableid = shmget(IDTABLE_SHMKEY, sizeof(int)*31, PERMS|IPC_CREAT);
    if(shmidTableid < 0){
        cout << "[Server]: shmget shmidTableid ERROR" << endl;
    }
    
    //attach shmidTable address
    shmidTable = (int*)shmat(shmidTableid, (char *)0, 0);
    if(shmidTable == (int *)-1){
        cout << "child:at shmidTable error" << endl;
    }
    //initial shmidTable to 0 to represent that all userID unused
    for(int i = 0; i < 30; i++){
        shmidTable[i] = 0;
    }
    
    //detach shmidTable address
    if(shmdt(shmidTable) != 0){
        cout << "[Server]: dt shmidTableid ERROR" << endl;
    }

    //create shmclientTable to save client information
    shmclientTableid = shmget(CLIENTTABLE_SHMKEY, sizeof(client_shm)*31, PERMS|IPC_CREAT);
    if(shmclientTableid < 0){
        cout << "[Server]: shmget shmclientTableid ERROR" << endl;
    }

    //create msgBuffer for receiving other clients msg
    msgBufferid = shmget(MSGBUFFER_SHMKEY, sizeof(char)*2000, PERMS|IPC_CREAT);
    if(msgBufferid < 0){
        cout << "[Server]: shmget msgBufferid ERROR" << endl;
    }

    shmUPidTableid = shmget(UPIDTABLE_SHMKEY, sizeof(int)*900, PERMS|IPC_CREAT);
    if(shmUPidTableid < 0){
        cout << "[Server]: shmget shmUPidTableid ERROR" << endl;
    }

    //attach shmUPidTable address
    shmUPidTable = (int*)shmat(shmUPidTableid, (char *)0, 0);
    if(shmUPidTable == (int *)-1){
        cout << "child:at shmUPidTable error" << endl;
    }
    //initial shmUPidTable to 0 to represent that all user_pipe_ID unused
    for(int i = 0; i < 900; i++){
        shmUPidTable[i] = 0;
    }
    
    //detach shmidTable address
    if(shmdt(shmUPidTable) != 0){
        cout << "[Server]: dt shmUPidTableid ERROR" << endl;
    }

    //create shmUserPipeTable for recording user_pipe_info
    shmUserPipeTableid = shmget(USERPIPETABLE_SHMKEY, sizeof(user_pipe_info)*900, PERMS|IPC_CREAT);
    if(shmUserPipeTableid < 0){
        cout << "[Server]: shmget shmUserPipeTableid ERROR" << endl;
    }

    cout << "Server create shm tables success" << endl;
}

void attach_shm_tables(){
    //attach shmclientTable address
    shmclientTable = (client_shm*)shmat(shmclientTableid, (char *)0, 0);
    if(shmclientTable == (client_shm *)(-1)){
        cout << "[Child]: attach shmclientTable error" << endl;
    }

    //attach shmidTable address
    shmidTable = (int*)shmat(shmidTableid, (char *)0, 0);
    if(shmidTable == (int *)(-1)){
        cout << "child:at shmidTable error" << endl;
    }

    //attach msgBuffer address
    msgbuffer = (char*)shmat(msgBufferid, (char *)0, 0);
    if(msgbuffer == (char *)(-1)){
        cout << "child:at msgbuffer error" << endl;
    }

    //attach shmUPidTable address
    shmUPidTable = (int*)shmat(shmUPidTableid, (char *)0, 0);
    if(shmUPidTable == (int *)(-1)){
        cout << "child:at shmUPidTable error" << endl;
    }

    //attach shmUserPipeTable address
    shmUserPipeTable = (user_pipe_info*)shmat(shmUserPipeTableid, (char *)0, 0);
    if(shmUserPipeTable == (user_pipe_info *)(-1)){
        cout << "child:at shmUserPipeTable error" << endl;
    }

    cout << "child attach shm tables success" << endl;
}

void detach_shm_tables(){
    //detach shmidTable address
    if(shmdt(shmidTable) != 0){
        cout << "[Child]: dt shmidTable ERROR" << endl;
    }

    //detach shmclientTable address
    if(shmdt(shmclientTable) != 0){
        cout << "[Child]: dt shmclientTable ERROR" << endl;
    }

    //detach msgBuffer address
    if(shmdt(msgbuffer) != 0){
        cout << "[Child]: dt msgbuffer ERROR" << endl;
    }

    if(shmdt(shmUPidTable) != 0){
        cout << "[Child]: dt shmUPidTable ERROR" << endl;
    }

    if(shmdt(shmUserPipeTable) != 0){
        cout << "[Child]: dt shmUserPipeTable ERROR" << endl;
    }
}

bool user_name_exist(string name){
    for(int i=0; i<30; i++){
        if(shmidTable[i] == 1 && strcmp(shmclientTable[i].name, name.c_str()) == 0){
            return true;
        }
    }
    return false;
}

bool user_exist(int id){
    if(shmidTable[id-1] == 1){
        return true;
    }
    return false;
}

bool user_pipe_exist(int sender_id, int recv_id){
    for(int i=0; i<900; i++){
        if(shmUPidTable[i] == 1 && shmUserPipeTable[i].sender_id == sender_id && shmUserPipeTable[i].recv_id == recv_id){
            return true;
        }
    }
    return false;
}

void broadcast_msg(string msg, client_shm *shmclientTable){
    char* tmp = strdup(msg.c_str());
    cout << "broadcast " << msg << endl;
    for(int i=0; i<30; i++){
        if(shmidTable[i] == 1){
            strcpy(msgbuffer, msg.c_str());
            kill(shmclientTable[i].p_id, SIGUSR1);
        }
    }
}

string get_client_name_by_id(int id){
    if(shmidTable[id-1] == 1){
        return shmclientTable[id-1].name;
    }
}

void close_connection_handler(client_shm &cur_client){
    string tmp(cur_client.name); 
    broadcast_msg(logout_msg(tmp), shmclientTable);
    shmidTable[cur_client.id-1] = 0;

    //delete related user pipe file
    for(int i=0; i<900; i++){
        string fileName= "user_pipe/";
        bool deleted = false;
        if(shmUserPipeTable[i].sender_id == cur_client.id){
            shmUPidTable[i] = 0;
            fileName.append(to_string(cur_client.id)+"_"+to_string(shmUserPipeTable[i].recv_id)+".txt");
            deleted = true;
        }else if(shmUserPipeTable[i].recv_id == cur_client.id){
            shmUPidTable[i] = 0;
            fileName.append(to_string(shmUserPipeTable[i].sender_id)+"_"+to_string(cur_client.id)+".txt");
            deleted = true;
        }
        if(deleted){
            if(remove(fileName.c_str()) == 0){
                cout << "delete file " << fileName << " success." << endl;
            }else{
                cout << "unable to delete " << fileName << endl;
            }
        }
    }

    close(cur_client.socket_fd);
    exit(0);
}

client_shm* new_client_handler(int cli_socketfd, struct sockaddr_in &cli_addr, client_shm *shmclientTable){
    
    setenv("PATH", "bin:.", 1);
    command_set = get_known_command_set();

    //send welcome message
    send(cli_socketfd,welcome_msg.c_str(),welcome_msg.length(),0);
    int min_id = assign_user_id();

    string default_name = "(no name)";
    shmclientTable[min_id].id = min_id+1;
    strcpy(shmclientTable[min_id].name, default_name.c_str());
    strcpy(shmclientTable[min_id].ip, "CGILAB/511");
    // strcpy(shmclientTable[min_id].ip, concat_ip(inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port)).c_str());
    shmclientTable[min_id].socket_fd = cli_socketfd;
    shmclientTable[min_id].p_id = getpid();
    cout << "new_client id = " << shmclientTable[min_id].id << endl;
    cout << "new_client socket_fd= " << shmclientTable[min_id].socket_fd << endl;
    
    string tmp(shmclientTable[min_id].ip);
    broadcast_msg(login_msg(shmclientTable[min_id].name, tmp), shmclientTable);
    send(cli_socketfd,"% ",3,0);
    return &(shmclientTable[min_id]);
}

void sig_handler(int signo, siginfo_t *info, void *context){
    int status;

    if(signo == SIGINT){
        detach_shm_tables();

        if(shmctl(shmclientTableid, IPC_RMID, NULL) == -1){
            cout << "[Server]: shmctl shmclientTableid ERROR" << endl;
        }
        if(shmctl(shmidTableid, IPC_RMID, NULL) == -1){
            cout << "[Server]: shmctl shmidTableid ERROR" << endl;
        }
        if(shmctl(msgBufferid, IPC_RMID, NULL) == -1){
            cout << "[Server]: shmctl msgBufferid ERROR" << endl;
        }
        if(shmctl(shmUserPipeTableid, IPC_RMID, NULL) == -1){
            cout << "[Server]: shmctl shmUserPipeTableid ERROR" << endl;
        }
        if(shmctl(shmUPidTableid, IPC_RMID, NULL) == -1){
            cout << "[Server]: shmctl shmUPidTableid ERROR" << endl;
        }

        cout << "[Server]: release all shared memory success" << endl;
        exit(0);
    }else if(signo == SIGUSR1){
        if(send(cli_socketfd, msgbuffer, strlen(msgbuffer), 0) < 0){
            cout << "send error" << endl;
        }
    }
}

void client_handler(client_shm &cur_client, client_shm *shmclientTable){
    char buf[buffersize];
    int cli_socketfd = cur_client.socket_fd;
    int n;
    int line_count = 0;
    vector<string> tokens;

    while((n = recv(cli_socketfd,buf,sizeof(buf),0)) != 0){
        string inputLine = buf;
        if(inputLine.find("\n") != string::npos){
            inputLine = inputLine.substr(0,inputLine.find("\n"));
        }
        
        if(inputLine == ""){
            //receive broadcast msg
            memset(buf, 0, sizeof(buf));
            continue;
        }

        if(inputLine != "\r"){
            line_count++;
        }

        if(inputLine.find("\r") != string::npos){
            int tmp = inputLine.find("\r");
            inputLine = inputLine.substr(0,tmp);
        }
        cout << "inputLine= " << inputLine << "..." << endl;
        //cout << "line:" <<line_count<<endl;
        if(inputLine.compare("exit") == 0){
            close_connection_handler(cur_client);
            break;
        }
        
        tokens = split_line(inputLine, " ");
        
        if(inputLine.find("setenv") == 0){
            setenv(tokens[1].c_str(), tokens[2].c_str(), 1);
            command_set = get_known_command_set();
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
                strcpy(cur_client.name, tokens[1].c_str());
                broadcast_msg(change_name_msg(tokens[1], cur_client.ip), shmclientTable);
            }
        }else if(inputLine.find("yell") == 0){
            string yell_content = inputLine.substr(inputLine.find("yell")+5);
            string tmp(cur_client.name);
            broadcast_msg(yell_msg(tmp, yell_content), shmclientTable);
        }else if(inputLine.find("tell") == 0){
            if(shmidTable[stoi(tokens[1])-1] == 1){
                
                string content = inputLine.substr(inputLine.find(tokens[1])+2);
                string tmp(cur_client.name);
                string msg = tell_msg(tmp, content);

                strcpy(msgbuffer, msg.c_str());
                kill(shmclientTable[stoi(tokens[1])-1].p_id, SIGUSR1);
            }else{
                //send user not exist error msg
                string tmp = user_not_exist_msg(tokens[1]);
                send(cur_client.socket_fd, tmp.c_str(), tmp.length(), 0);
            }
        }else if(inputLine.compare("who") == 0){
            string info = who_msg_shm(shmidTable, shmclientTable, cur_client.id);
            send(cli_socketfd, info.c_str(), info.length(), 0);
        }else{
            parse_cmd(cli_socketfd, line_count, tokens, cur_client, inputLine);
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
                dup2(cli_socketfd, STDOUT_FILENO);
                dup2(cli_socketfd, STDERR_FILENO);
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
                    }else if(it->is_user_pipe_out){
                        string fileName= "user_pipe/"; 
                        fileName.append(to_string(cur_client.id)+"_"+to_string(it->user_pipe_dest_id)+".txt");
                        set_write_file_output(fileName);
                    }
                }

                if(it->is_user_pipe_in){
                    string fileName= "user_pipe/"; 
                    fileName.append(to_string(it->user_pipe_src_id)+"_"+to_string(cur_client.id)+".txt");
                    set_read_file(fileName);
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
                }
                
                //close previous job pipe
                if(it != current_job_queue.begin()){
                    
                    command prev_cmd = *(it-1);
                    close((it-1)->pipe_arr[0]);
                    close((it-1)->pipe_arr[1]);
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
        memset(buf, 0, sizeof(buf));
        send(cli_socketfd,"% ",3,0);
    }
}