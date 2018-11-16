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
    bool is_user_pipe_out = false;
    bool is_user_pipe_in = false;
    int user_pipe_arr[2] = {-1,-1};
    int user_pipe_src_id;
    int user_pipe_dest_id;
};

struct unhandled_pipe_obj{
    int exe_line_num;
    int pipe_arr[2];
};


bool operator <(const unhandled_pipe_obj& x, const unhandled_pipe_obj& y){
    return x.exe_line_num < y.exe_line_num;
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

// >n
bool is_write_to_user_pipe(string token){
    if(token.find(">") == 0 && token.size() > 1) return true;
    return false;
}

// <n
bool is_read_from_user_pipe(string token){
    if(token.find("<") == 0 && token.size() > 1) return true;
    return false;
}

void childHandler(int signo){
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0){}
}
