#pragma once
#include <windows.h>
#include <string>
#include <utility>

// One temporary selection per application process, shared by its TSF threads.
// Called on focus/menu/timer callbacks; never from the input-key path.
class ApplicationGroup {
    struct Shared {
        SRWLOCK lock=SRWLOCK_INIT;
        std::string id;
        ULONGLONG revision=0;
    };
    static Shared& state(){static Shared value;return value;}
    class Guard {
        SRWLOCK* lock_;
        bool exclusive_;
    public:
        Guard(SRWLOCK& lock,bool exclusive):lock_(&lock),exclusive_(exclusive){if(exclusive_)AcquireSRWLockExclusive(lock_);else AcquireSRWLockShared(lock_);}
        ~Guard(){if(exclusive_)ReleaseSRWLockExclusive(lock_);else ReleaseSRWLockShared(lock_);}
        Guard(const Guard&)=delete;
        Guard& operator=(const Guard&)=delete;
    };
public:
    struct Selection{std::string id;ULONGLONG revision;};
    static Selection get(){auto& value=state();Guard guard(value.lock,false);return {value.id,value.revision};}
    static void select(std::string id){auto& value=state();Guard guard(value.lock,true);if(value.id!=id){value.id.swap(id);++value.revision;}}
    static void reset_if(const Selection& previous){auto& value=state();Guard guard(value.lock,true);if(value.revision==previous.revision && value.id==previous.id){value.id.clear();++value.revision;}}
};
