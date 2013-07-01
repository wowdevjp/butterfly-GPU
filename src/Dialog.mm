//
//  Dialog.cpp
//  butterfly
//
//  Created by yoshimura atsushi on 2013/06/21.
//
//

#include "Dialog.h"
#import <Cocoa/Cocoa.h>
namespace wowdev
{
    Dialog::Dialog(const std::string &message):m_message(message)
    {
        
    }
    void Dialog::show()const
    {
        NSRunAlertPanel(@"message",
                        [NSString stringWithCString:m_message.c_str() encoding:NSUTF8StringEncoding],
                        @"ok", NULL, NULL);
    }
}