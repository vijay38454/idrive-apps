// SPDX-License-Identifier: GPL-3.0-or-later
/* Writes every kind of turn card, over a map-coloured ground, as one PAM
 * picture (RGB_ALPHA), to look at the drawing; "compact" the compact cards
 * (below the arrow):
 *
 *   make build/host/turn_card_preview && build/host/turn_card_preview [compact] > cards.pam
 */
#include <cstdio>
#include <string>
#include <vector>

#include "turn_card.h"

using namespace woc;

int main(int argc, char **argv)
{
    const bool compact = argc > 1 && std::string(argv[1]) == "compact";
    const unsigned ch = compact ? kTurnCardCompactH : kTurnCardH;
    struct Case {
        Maneuver::Kind kind;
        int exit;
        const char *dist, *road;
        bool left;        /* where traffic keeps left: roundabouts clockwise */
        int number;       /* a roundabout's exit */
    } cases[] = {
        {Maneuver::Straight, 0, "1,2 km", "Avenida Exemplo", false, 0},
        {Maneuver::SlightLeft, 0, "300 m", "Rua das Ac\xC3\xA1" "cias", false, 0},
        {Maneuver::Left, 0, "200 m", "Rua Jo\xC3\xA3o das Flores, \xC3\x81rvore, S\xC3\xA3o Jos\xC3\xA9", false, 0},
        {Maneuver::SharpLeft, 0, "90 m", "Travessa Curta", false, 0},
        {Maneuver::SlightRight, 0, "450 m", "Estrada Velha", false, 0},
        {Maneuver::Right, 0, "20 m", "R. Exemplo", false, 0},
        {Maneuver::SharpRight, 0, "60 m", "Rua Diagonal", false, 0},
        {Maneuver::UTurn, 0, "150 m", "Retorno", false, 0},
        {Maneuver::UTurnRight, 0, "150 m", "U-turn", false, 0},
        {Maneuver::KeepLeft, 0, "800 m", "Marginal Esquerda", false, 0},
        {Maneuver::KeepRight, 0, "800 m", "Marginal Direita", false, 0},
        {Maneuver::ExitLeft, 0, "2 km", "Sa\xC3\xAD" "da 12", false, 0},
        {Maneuver::ExitRight, 0, "1,5 km", "Sa\xC3\xAD\x64\x61 7 - Centro", false, 0},
        {Maneuver::Roundabout, 90, "100 m", "1\xC2\xAA sa\xC3\xAD\x64\x61", false, 0},
        {Maneuver::Roundabout, 0, "100 m", "2\xC2\xAA sa\xC3\xAD\x64\x61", false, 0},
        {Maneuver::Roundabout, -90, "100 m", "3\xC2\xAA sa\xC3\xAD\x64\x61", false, 0},
        {Maneuver::Roundabout, 180, "100 m", "Retorno na rotat\xC3\xB3ria", false, 0},
        {Maneuver::Roundabout, 45, "100 m", "Rotat\xC3\xB3ria", false, 0},
        {Maneuver::Arrive, 0, "0 m", "Casa de Teste", false, 0},
        {Maneuver::ArriveLeft, 0, "50 m", "Destino \xC3\xA0 esquerda", false, 0},
        {Maneuver::ArriveRight, 0, "50 m", "Destino \xC3\xA0 direita", false, 0},
        {Maneuver::Ferry, 0, "70 m", "Balsa", false, 0},
        {Maneuver::Other, 0, "70 m", "Outro", false, 0},
        {Maneuver::Right, 0, "200 m", "", false, 0},
        {Maneuver::Left, 0, "1.234,5 km", "\xD0\x9B\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xBD\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 \xD0\xBF\xD1\x80\xD0\xBE\xD1\x81\xD0\xBF\xD0\xB5\xD0\xBA\xD1\x82 / Very long road name that will not fit", false, 0},
        /* a UK route: what carplay sends where traffic keeps left */
        {Maneuver::Straight, 0, "5 yd", "Start on High Street", true, 0},
        {Maneuver::Left, 0, "0.2 mi", "Turn left onto A231", true, 0},
        {Maneuver::SlightRight, 0, "0.3 mi", "Slight right onto the slip road to M3", true, 0},
        {Maneuver::UTurnRight, 0, "100 yd", "Make a U-turn", true, 0},
        {Maneuver::Roundabout, -90, "0.8 mi", "Take the 1st exit onto A33", true, 1},
        {Maneuver::Roundabout, 0, "0.4 mi", "Take the 2nd exit onto A289", true, 2},
        {Maneuver::Roundabout, 90, "0.7 mi", "Take the 2nd exit onto M2", true, 2},
        {Maneuver::Roundabout, 135, "0.7 mi", "Take the 3rd exit onto A228", true, 3},
        {Maneuver::Roundabout, 180, "200 yd", "Take the 4th exit onto A2", true, 0},
        {Maneuver::KeepLeft, 0, "10 mi", "Keep left for M25 (S) / M23", true, 0},
        {Maneuver::KeepLeft, 0, "220 yd", "Merge onto A1(M)", true, 0},
        {Maneuver::KeepRight, 0, "350 ft", "Use the right lane for B2004", true, 0},
        {Maneuver::KeepLeft, 0, "0.5 mi", "At junction 5, take the slip road to A41", true, 0},
        {Maneuver::Ferry, 0, "21 mi", "Take the ferry to Calais", true, 0},
        {Maneuver::ArriveLeft, 0, "50 yd", "Destination", true, 0},
        {Maneuver::Left, 0, "0.5 mi", "Turn left onto A3024 towards a very long place name", true, 0},
    };
    const int n = sizeof cases / sizeof cases[0], cols = 2, pad = 12;
    const int rows = (n + cols - 1) / cols;
    const int W = cols * (int)kTurnCardW + (cols + 1) * pad, H = rows * (int)ch + (rows + 1) * pad;
    std::vector<uint8_t> img((size_t)W * H * 4);
    for (int i = 0; i < W * H; i++) {
        img[i * 4] = 92, img[i * 4 + 1] = 110, img[i * 4 + 2] = 96, img[i * 4 + 3] = 255;
    }
    for (int k = 0; k < n; k++) {
        Maneuver m;
        m.kind = cases[k].kind;
        m.exit_deg = cases[k].exit;
        m.distance = cases[k].dist;
        m.road = cases[k].road;
        m.clockwise = cases[k].left;
        m.exit_number = cases[k].number;
        std::vector<uint8_t> card;
        render_turn_card(m, &card, compact);
        const int ox = pad + (k % cols) * ((int)kTurnCardW + pad), oy = pad + (k / cols) * ((int)ch + pad);
        for (unsigned y = 0; y < ch; y++)
            for (unsigned x = 0; x < kTurnCardW; x++) {
                const uint8_t *s = &card[(y * kTurnCardW + x) * 4];
                uint8_t *d = &img[((size_t)(oy + y) * W + ox + x) * 4];
                const float a = s[3] / 255.0f;
                for (int c = 0; c < 3; c++)
                    d[c] = (uint8_t)(s[c] * a + d[c] * (1 - a) + 0.5f);
            }
    }
    std::printf("P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", W, H);
    std::fwrite(img.data(), 1, img.size(), stdout);
    return 0;
}
