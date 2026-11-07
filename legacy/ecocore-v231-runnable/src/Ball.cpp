#include "Ball.h"

char ballChar(BallType type) {
    switch (type) {
        case BallType::Shield: return 's';
        case BallType::Worker: return 'w';
        case BallType::Scout:  return 'c';
    }
    return '?';
}

