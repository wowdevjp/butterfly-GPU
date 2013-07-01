//
//  Dialog.h
//  butterfly
//
//  Created by yoshimura atsushi on 2013/06/21.
//
//

#ifndef __butterfly__Dialog__
#define __butterfly__Dialog__

#include <string>
namespace wowdev
{
    class Dialog
    {
    public:
        Dialog(const std::string &message);
        void show()const;
    private:
        std::string m_message;
    };
}

#endif /* defined(__butterfly__Dialog__) */
