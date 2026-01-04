//SELECT data FROM offlinemsg WHERE uid = 1234567891 ORDER BY msg_no ASC;
#ifndef HANDLER_USER_HPP
#define HANDLER_USER_HPP

#include "../HeadFile.h"

class OfflineHandler {
    static shared_mutex mx;
public:



};
shared_mutex OfflineHandler::mx;
#endif