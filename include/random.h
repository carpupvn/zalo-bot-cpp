#ifndef RANDOM_H
#define RANDOM_H

#include <random>
#include <string>

namespace myRandom {
    static std::mt19937 gen(std::random_device{}());

    inline int RanInt(int a, int b) {
        std::uniform_int_distribution<int> dist(a, b);
        return dist(gen);
    }

    inline int Dice() {
        return RanInt(1, 6);
    }

    inline double RandFloat(double a, double b) {
        std::uniform_real_distribution<double> dist(a, b);
        return dist(gen);
    }

    inline std::string RanStr(int length) {
        static const std::string chars =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789";

        std::uniform_int_distribution<int> dist(0, chars.size() - 1);

        std::string result;
        result.reserve(length);

        for (int i = 0; i < length; i++)
            result += chars[dist(gen)];

        return result;
    }

    template<typename... Args>
    inline std::string RanStrList(const std::string& first, const Args&... rest) {
        const std::string arr[] = { first, rest... };
        int size = sizeof...(rest) + 1;
        std::uniform_int_distribution<int> dist(0, size - 1);
        return arr[dist(gen)];
    }
}

#endif
