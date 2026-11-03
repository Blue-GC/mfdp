#include <stdio.h>
#include "util.h"

#define MAX_ITEMS 16

typedef struct Point {
    int x;
    int y;
} Point;

enum Color { RED, GREEN };

static int counter = 0;

int add(int a, int b) { return a + b; }

/* Scale a point. */
Point scale(Point p, int k) {
    p.x = add(p.x, k);
    counter++;
    return p;
}
