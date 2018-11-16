#define buffersize 15000
#define maxClientNum 30
#define PERMS 0666
#define CLIENTTABLE_SHMKEY ((key_t) 7671) //base value for shmclientTable key
#define IDTABLE_SHMKEY ((key_t) 7672) // base value for shmidTable key
#define MSGBUFFER_SHMKEY ((key_t) 7673) // base value for msgbuffer key
#define USERPIPETABLE_SHMKEY ((key_t) 7674) // base value for shmUserPipeTable key
#define UPIDTABLE_SHMKEY ((key_t) 7675) // base value for shmUPidTable key

struct client_shm{
    int id;
    int p_id;
    char name[50];
    char ip[50];
    int socket_fd;
};

string who_msg_shm(int *shmidTable, client_shm *shmclientTable, int id){
    string tmp = "<ID> <nickname>    <IP/port>    <indicate me>\n";
    for(int i=0; i<30; i++){
        if(shmidTable[i] == 1){
            if(shmclientTable[i].id == id){
                tmp.append(to_string(id)+ "    " + shmclientTable[i].name + "     " + shmclientTable[i].ip + "    <-me\n");
            }else{
                tmp.append(to_string(shmclientTable[i].id)+ "    " + shmclientTable[i].name + "     " + shmclientTable[i].ip + "\n");
            }
        }
    }
    return tmp;
}