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