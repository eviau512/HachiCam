#include "paper_math.h"
#include "fusion_math.h"
#include <iostream>
#include <stdexcept>
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    for (bool gentle : {false, true}) {
        for (float background : {0.f, 80.f, 120.f, 200.f, 240.f, 255.f}) {
            float last = -1;
            for (int l = 0; l <= 255; ++l) {
                const float value = paper::luminance(l, background, 0, gentle);
                check(std::isfinite(value) && value >= 0 && value <= 255, "Finite bounded output");
                check(value > last, "Continuous tonal order must not flatten faint marks");
                last = value;
            }
        }
        const float paper = paper::luminance(235, 235, 0, gentle);
        const float pencil = paper::luminance(230, 235, 0, gentle);
        check(paper - pencil >= 4, "Pale pencil cannot collapse into white");
        check(paper < 252, "Paper retains white headroom");
        check(paper::luminance(60, 80, 0, gentle) == 60, "Dark paper does not undergo whitening");
        check(paper::luminance(130, 200, 48, gentle) < paper::luminance(130, 200, 0, gentle), "Colored ink reduces whitening strength");
    }
    check(!fusion::phasesCover2D({{.3, .3}, {-.3, -.3}}), "Collinear phases do not establish two-dimensional support");
    check(!fusion::phasesCover2D({{0, 0}, {0, 0}}), "Identical phases cannot enable SR");
    check(fusion::phasesCover2D({{.35, .1}, {.1, .35}}), "Independent phases supply two-dimensional support");
    std::cout << "Paper tone and phase coverage tests passed\n";
}
