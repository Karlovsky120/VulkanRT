#include "application.h"

#include "common.h"

#include <stdexcept>

int main(int, char*[]) {
    Application application;
    try {
        application.run();
    } catch (std::runtime_error e) {
        printf("%s/n", e.what());
        return -1;
    }

    return 0;
}
