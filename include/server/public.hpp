#ifndef PUBLIC_HANDFILE
#define PUBLIC_HANDFILE
#include "HeadFile.h"
//Server与Client共有的类与数据                MsgType                           Server                                                      Client
enum MsgType{
    LOGIN_MSG = 1,      //登录消息              1               userid   username   password                |           ErrNo   OffLineMsgNum   OffLineMesg  FriendNum  Friend  FriendReqestNum    FriendRequest
    //LOGIN_ACK,          //登录确认              2
    REGIST_MSG,         //注册消息              3               username    password                        |           ErrNo   userid  message
    //REGIST_ACK,         //注册确认              4
    DROP_MSG,           //掉线消息              5               userid                                      |           if fail(ErrNo   message)
    //DROP_ACK,           //掉线确认              6
    ONE_CHAT_MSG,       //聊天消息              7               fromid  fromname  toid  message             |           if fail(ErrNo   message)
    FRIEND_REQ_MSG,     //添加好友请求          8               fromid  toid   message                      |           ErrNO   toid    toname  if fail(message)
    FRIEND_REQ_ACK,     //添加好友确认          9                
    FRIEND_ACC_MSG,     //同意添加好友          10              fromid  userid 
    FRIEND_ACC_ACK,     //同意添加好友确认      11
    FRIEND_DEL_MSG,     //删除好友消息          12              friendid  userid
    FRIEND_DEL_ACK,     //删除好友确认          13
    FRIEND_UNACC_MSG,   //拒绝好友申请          14              fromid  userid
    GROUP_CREATE_MSG,   //创建群聊消息          15              userid  groupname   desc                    |           groupid
    GROUP_REQ_MSG,      //请求加入群聊消息      16              groupid reqid   message                     
    GROUP_ACC_MSG,      //同意加入群聊消息      17              groupid reqid
    GROUP_ACC_ACK,
    GROUP_CHAT_MSG,     //群组聊天消息          18              groupid fromid fromname message
    GROUP_REMOVE_MSG,   //退出群聊消息          19              groupid userid
    GROUP_REFUSE_MSG,   //拒绝加入群聊申请      20              groupid reqid
    GROUP_ROLE_MSG,     //切换用户权限消息      21              groupid userid role
    GROUP_INFO_MSG,     //获取群组消息          22              groupid userid
    TIP_MSG,            //通知消息              23                                                                       ErrNO  Message
    FRIEND_ACC_TO_ACK,  //同意添加好友本地确认  24              fromid fromname
    FRIEND_ACC_FROM_ACK,//同意添加好友对方确认  25
    GROUP_REMOVE_ACK
};
#endif // !PUBLIC_HANDFILE

/*

    LOGIN_MSG:
    {
        "MsgType":1,
        "id":xxx,
        "password":"xxxxx"
    }

    LOGIN_ACK:
    {
        "MsgType":2,
        ("ErrNo":0,"Id":xxx,"name":"xxxx","OffLineMsgNum":xxx(,"OffLineMsg":[....]))
        ("ErrNo":1,"Errmsg":"xxxxx")
    }

    REGISET_MSG:
    {
        "MsgType":3,
        "name":"xxx",
        "password":"xxxxx"
    }

    REGISET_ACK:
    {
        "MsgType":4,
        "ErrNo":xxx,
        ("Id":xxx)
    }

    DROP_Msg:
    {
        "MsgType":5,
        "id":xxx
    }

    DROP_ACK:
    {
        "MsgType":6,
        ("ErrNo":0,"Id":xxx)
        ("ErrNo":1,"ErrMsg":"xxxx")
    }

    ONE_CHAT__MSG:
    {
        "MsgType":7,
        "fromid":xxx,
        "toid":xxx,
        "Msg":"xxxxxx"
    }

    FRIEND_REQ_MSG:
    {
        "MsgType":8,
        "fromid":xxx,
        "id":xxx,
        "message":"xxxxx"
    }

    FRIEND_ACC_MSG:
    {
        "MsgType":10,
        "userid":xxx,
        "friendid":xxx
    }

    FRIEND_DEL_MSG:
    {
        "MsgType":12,
        "userid":xxx,
        "friendid":xxx
    }

*/