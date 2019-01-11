//
// Created by divan on 08/01/19.
//

#pragma once

union irect {
    struct {
        int x, y, w, h;
    };
};

inline irect Rect(int x, int y, int w, int h) {
    irect result;
    result.x = x;
    result.y = y;
    result.w = w;
    result.h = h;
    return result;
}

bool inside(irect r, int x, int y) {
   return (r.x <= x && r.x + r.w >= x && r.y <= y && r.y + r.h >= y);
}