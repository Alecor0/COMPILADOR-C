#include <iostream>
#include <string>

int sumar(int a, int b) {
    return a + b;
}

double sumar(double a, double b) {
    return a + b;
}

int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

std::string etiqueta(std::string texto) {
    return "Resultado: " + texto;
}

int main() {
    auto total = sumar(4, 5);
    bool mascaraCorrecta = (13 & 5) == 5;

    std::cout << etiqueta("suma = 9") << std::endl;
    std::cout << "factorial(5) = " << factorial(5) << std::endl;
    std::cout << "sobrecarga double = " << sumar(1.5, 2.5) << std::endl;

    if (mascaraCorrecta) {
        std::cout << "operador bit a bit: correcto" << std::endl;
    }

    return total == 9 ? 0 : 1;
}
