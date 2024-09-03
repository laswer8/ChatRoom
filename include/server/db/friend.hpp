#ifndef FRIEND
#define FRIEND

#include "../HeadFile.h"

class Friend{
    private:
        int myid;
        int friendid;
    public:
        void setmyid(int id){myid = id;}
        void setfriendid(int id){friendid = id;}
        int getmyid(){return myid;}
        int getfriendid(){return friendid;}
};

class FriendReq{
    private:
        int id;
        string name;
        int fromid;
        string jsonmsg;
    public:
        void setid(int _id){id = _id;}
        void setname(string _name){name = _name;}
        void setfromid(int _id){fromid = _id;}
        void setjsonmsg(string msg){jsonmsg = msg;}
        int getid(){return id;}
        string getname(){return name;}
        int getfromid(){return fromid;}
        string getjsonmsg(){return jsonmsg;}
};


#endif // !FRIEND