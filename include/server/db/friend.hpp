#ifndef FRIEND
#define FRIEND

#include "../HeadFile.h"

class Friend{
    private:
        string username;
        string friendname;
    public:
        void setusername(const string& _username){username = _username;}
        void setfriendname(const string& _username){friendname = _username;}
        string getusername(){return username;}
        string getfriendname(){return friendname;}
};

class FriendReq{
    private:
        string username;
        string name;
        string fromname;
        string jsonmsg;
    public:
        void setusername(string _username){username = _username;}
        void setname(string _name){name = _name;}
        void setfromname(string _fromname){fromname = _fromname;}
        void setjsonmsg(string msg){jsonmsg = msg;}
        string getusername(){return username;}
        string getname(){return name;}
        string getfromname(){return fromname;}
        string getjsonmsg(){return jsonmsg;}
};


#endif // !FRIEND