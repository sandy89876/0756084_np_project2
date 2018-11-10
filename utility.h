#include <cstring>
using namespace std;

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
    set<string> command_set;
    deque<command> current_job_queue;
    set<unhandled_pipe_obj> unhandled_pipe_obj_set;
    int line_count = 0;
    map<string, string> env_setting = {{"PATH","bin:."}};
};

char *convert(const string & s)
{
    char *pc = new char[s.size()+1];
    std::strcpy(pc, s.c_str());
    return pc;
}

// |n
bool is_stdout_numbered_pipe(string token){
    if(token.find("|") == 0 && token.size() > 1) return true;
    return false;
}

// !n
bool is_stderr_numbered_pipe(string token){
    if(token.find("!") == 0 && token.size() > 1) return true;
    return false;
}

bool is_ordinary_pipe(string token){
	
	if(token=="|") return true;
    return false;
}

bool is_output_to_file(string token){
    if(token.compare(">") == 0) return true;
    return false;
}

void childHandler(int signo){
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0){}
}

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

string pipe_to_user_msg(string sender_name, int sender_id, string recv_name, int recv_id, command cmd){
    //string tmp = stars + " " + sender_name + " (#" + sender_id + ") just piped '" + cmd.name + 
}