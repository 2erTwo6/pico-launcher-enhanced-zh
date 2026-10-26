#pragma once
#include "core/math/Rgb.h"

// The markers keep these on every theme: red and green read as "favorite" and
// "done" whatever color the theme is built on, and the star that marks the
// most launched game is a prize, so it stays gold.
constexpr Rgb<8, 8, 8> kFavoriteRed(214, 40, 57);
constexpr Rgb<8, 8, 8> kCompletedGreen(67, 160, 71);

// Dark amber to bright gold: the star's own shading.
constexpr Rgb<8, 8, 8> kStarOutlineColor(90, 56, 8);
constexpr Rgb<8, 8, 8> kStarFillColor(255, 205, 50);
