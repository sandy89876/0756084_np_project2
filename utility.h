#include <cstring>
using namespace std;

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
