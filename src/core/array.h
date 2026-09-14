#pragma once

#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define array(name, type) \
    type* name;           \
    u32 name##_count

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Iterate over an array defined with the array() macro
///
/// item is the singular name: the container members it reads are items and items_count.
#define for_each_array(type, item, obj)                                                                 \
    for (u32 CONCAT(_i_, __LINE__) = 0, CONCAT(_count_, __LINE__) = (obj)->item##s_count;               \
         CONCAT(_i_, __LINE__) < CONCAT(_count_, __LINE__); CONCAT(_i_, __LINE__)++)                    \
        for (type* item = &(obj)->item##s[CONCAT(_i_, __LINE__)], *CONCAT(_once_, __LINE__) = (void*)1; \
             CONCAT(_once_, __LINE__); CONCAT(_once_, __LINE__) = 0)
