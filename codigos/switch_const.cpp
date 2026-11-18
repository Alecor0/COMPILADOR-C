#include <iostream>
#include <string>

int puntaje(int nivel) {
    switch (nivel) {
        case 1: return 10;
        case 2: return 25;
        case 3: return 50;
        default: return 0;
    }
}

int main() {
    const int nivel = 2;
    const std::string jugador = "Ada";

    switch (nivel) {
        case 1:
            std::cout << jugador << ": principiante" << std::endl;
            break;
        case 2:
            std::cout << jugador << ": intermedio" << std::endl;
            break;
        default:
            std::cout << jugador << ": otro nivel" << std::endl;
            break;
    }

    std::cout << "Puntos: " << puntaje(nivel) << std::endl;
    return 0;
}
