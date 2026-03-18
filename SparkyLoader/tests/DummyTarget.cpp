#include <windows.h>
#include <iostream>

int main() {
    std::cout << "Dummy Target Process Started." << std::endl;
    std::cout << "Target PID: " << GetCurrentProcessId() << std::endl;
    std::cout << "Waiting for injection. Press ENTER to close this target window..." << std::endl;
    std::cin.get();
    return 0;
}
