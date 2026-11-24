// SPDX-License-Identifier: GPL-3.0-or-later
/* Host test of the turn card (turn_card.h), its text (card_font.h) and
 * its setting (turn_card_setting.h): the kinds by name; the text decoded
 * from UTF-8, accents kept, a letter the font lacks folded or '?', a line
 * too wide cut with an ellipsis; a card with a plate, an arrow and text, a
 * plain mark for an arrow not known, and none for no manoeuvre; mirrored
 * arrows; roads not taken grey; a change of the distance alone touching
 * only the text's rows; a roundabout driven round clockwise the mirror of
 * one driven round anticlockwise; the exit's number in the ring, on both
 * cards, none for exit 0; a ferry's boat; the compact card, all on one line; the app's
 * example over the navigation's manoeuvre until it ends, on its own once
 * its time is up (card_choice.h); the setting off by default, the app's choice kept
 * across a restart, other values refused or read as off. */
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "card_choice.h"
#include "card_font.h"
#include "turn_card.h"
#include "turn_card_setting.h"

using namespace woc;

static int failures;

static void ok(const char *name, bool cond)
{
    std::printf("%-4s %s\n", cond ? "ok" : "FAIL", name);
    if (!cond) failures++;
}

static const uint8_t *at(const std::vector<uint8_t> &c, unsigned x, unsigned y)
{
    return &c[((size_t)y * kTurnCardW + x) * 4];
}

/* grey (a road not taken, 118,126,140) pixels in a rectangle */
static unsigned grey(const std::vector<uint8_t> &c, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    unsigned n = 0;
    for (unsigned y = y0; y < y1; y++)
        for (unsigned x = x0; x < x1; x++) {
            const uint8_t *p = at(c, x, y);
            n += p[0] >= 112 && p[0] <= 124 && p[2] >= 134 && p[2] <= 146 && p[3] > 240;
        }
    return n;
}

/* white pixels in a rectangle */
static unsigned white(const std::vector<uint8_t> &c, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    unsigned n = 0;
    for (unsigned y = y0; y < y1; y++)
        for (unsigned x = x0; x < x1; x++)
            n += at(c, x, y)[0] > 240 && at(c, x, y)[3] > 240;
    return n;
}

/* the road's light blue-grey text (200,208,220) in a rectangle, its glyphs'
 * cores: light, bluer than red, not white */
static unsigned light(const std::vector<uint8_t> &c, unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    unsigned n = 0;
    for (unsigned y = y0; y < y1; y++)
        for (unsigned x = x0; x < x1; x++) {
            const uint8_t *p = at(c, x, y);
            n += p[0] > 150 && p[0] <= 240 && p[2] > p[0] && p[3] > 200;
        }
    return n;
}

/* the icon's square (6..54, 4..52 on a full card) mirrored left to right
 * in b, pixel for pixel */
static bool icon_mirrored(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    for (unsigned y = 4; y < 52; y++)
        for (unsigned x = 6; x < 54; x++)
            if (std::memcmp(at(a, x, y), at(b, 6 + 53 - x, y), 4))
                return false;
    return true;
}

int main()
{
    Maneuver::Kind k = Maneuver::None;
    ok("kinds by name", maneuver_kind("left", &k) && k == Maneuver::Left && maneuver_kind("uturn", &k) &&
                            k == Maneuver::UTurn && maneuver_kind("none", &k) && k == Maneuver::None &&
                            maneuver_kind("other", &k) && k == Maneuver::Other && maneuver_kind("keep_right", &k) &&
                            k == Maneuver::KeepRight && maneuver_kind("arrive_left", &k) && k == Maneuver::ArriveLeft &&
                            maneuver_kind("exit_left", &k) && k == Maneuver::ExitLeft &&
                            maneuver_kind("uturn_right", &k) && k == Maneuver::UTurnRight &&
                            maneuver_kind("ferry", &k) && k == Maneuver::Ferry);
    ok("an unknown kind: refused", !maneuver_kind("sideways", &k) && !maneuver_kind("", &k));

    const CardFont &road = card_font_road(), &dist = card_font_distance();
    auto points = [](const std::vector<CardRun> &runs) {
        std::vector<uint32_t> out;
        for (const CardRun &r : runs)
            out.push_back(r.g->cp);
        return out;
    };
    int w = 0;
    ok("text: UTF-8, accents kept",
       points(card_line(road, "S\xC3\xA3o Jos\xC3\xA9 \xC3\x81rvore \xC4\x8C", 999, &w)) ==
           std::vector<uint32_t>({'S', 0xE3, 'o', ' ', 'J', 'o', 's', 0xE9, ' ', 0xC1, 'r', 'v', 'o', 'r', 'e', ' ', 0x10C}) &&
           w > 0);
    ok("text: Cyrillic in the road's font",
       points(card_line(road, "\xD0\x9C\xD0\xB8\xD1\x80", 999, nullptr)) == std::vector<uint32_t>({0x41C, 0x438, 0x440}));
    ok("text: a Vietnamese letter folded, an emoji and a stray byte as ?",
       points(card_line(road, "\xE1\xBA\xBF \xF0\x9F\x9A\x97 a\xC3", 999, nullptr)) ==
           std::vector<uint32_t>({'e', ' ', '?', ' ', 'a', '?'}));
    ok("text: the distance's font is ASCII: an accent as ?",
       points(card_line(dist, "2\xC2\xBD km", 999, nullptr)) == std::vector<uint32_t>({'2', '?', ' ', 'k', 'm'}));
    ok("text: spaces and controls as one space, none at the ends",
       points(card_line(road, "  a \t\n b  ", 999, nullptr)) == std::vector<uint32_t>({'a', ' ', 'b'}));
    int full = 0, cut = 0;
    card_line(road, "Avenida Longa Ficticia", 999, &full);
    const std::vector<CardRun> c100 = card_line(road, "Avenida Longa Ficticia", 100, &cut);
    ok("text: too wide, cut with an ellipsis within the room",
       full > 100 && cut <= 100 && cut > 60 && !c100.empty() && c100.back().g->cp == 0x2026 &&
           c100[c100.size() - 2].g->cp != ' ');
    int fit = 0;
    ok("text: one that fits is whole", points(card_line(road, "Av. Brasil", 999, &fit)).size() == 10 &&
                                           points(card_line(road, "Av. Brasil", fit, nullptr)).size() == 10);
    ok("text: nothing, no glyphs", card_line(road, "  ", 999, &w).empty() && w == 0);

    std::vector<uint8_t> none, left, right, far;
    Maneuver m;
    render_turn_card(m, &none);
    bool empty = none.size() == (size_t)kTurnCardW * kTurnCardH * 4;
    for (uint8_t b : none)
        empty = empty && b == 0;
    ok("no manoeuvre: a clear card", empty);

    m.kind = Maneuver::Left;
    m.distance = "200 m";
    m.road = "Av. Exemplo Comprida";
    render_turn_card(m, &left);
    ok("the plate: dark and nearly opaque in the middle, clear at the corners",
       at(left, 200, 30)[3] > 200 && at(left, 200, 30)[0] < 40 && at(left, 0, 0)[3] == 0 &&
           at(left, kTurnCardW - 1, kTurnCardH - 1)[3] == 0);
    ok("the arrow: white in the left square", white(left, 6, 4, 54, 52) > 150);
    ok("the distance and the road: text right of the arrow", white(left, 62, 7, 200, 28) > 100);
    m.kind = Maneuver::Right;
    render_turn_card(m, &right);
    /* the arrows centred in the square: the head's side and the far edge */
    ok("left and right: mirrored arrows",
       white(left, 6, 4, 24, 52) > 20 && white(left, 46, 4, 54, 52) == 0 && white(right, 36, 4, 54, 52) > 20 &&
           white(right, 6, 4, 14, 52) == 0);
    m.distance = "150 m";
    render_turn_card(m, &far);
    bool rows = true, changed = false;
    for (unsigned y = 0; y < kTurnCardH; y++)
        for (unsigned x = 0; x < kTurnCardW; x++)
            if (std::memcmp(at(right, x, y), at(far, x, y), 4)) {
                changed = true;
                rows = rows && y >= 7 && y < 28 && x >= 62;
            }
    ok("another distance: only the distance's rows change", changed && rows);
    std::vector<uint8_t> other;
    m.kind = Maneuver::Other;
    render_turn_card(m, &other);
    ok("an arrow not known: a dot in the middle of the square, no stroke",
       white(other, 22, 20, 38, 36) > 60 && white(other, 6, 42, 54, 52) == 0 && white(other, 6, 4, 54, 14) == 0 &&
           white(other, 62, 7, 200, 28) > 100);

    std::vector<uint8_t> keep, rb_right, rb_left, rb_back, flag, sharp;
    m.kind = Maneuver::KeepRight;
    render_turn_card(m, &keep);
    ok("keep right: the left branch grey, the head white on the right",
       grey(keep, 6, 4, 30, 30) > 20 && white(keep, 30, 4, 54, 20) > 40 && white(keep, 6, 4, 24, 20) == 0);
    m.kind = Maneuver::Roundabout;
    m.exit_deg = 90;
    render_turn_card(m, &rb_right);
    ok("roundabout, the first exit right: the ring's left grey, the arm and head right",
       grey(rb_right, 6, 14, 30, 44) > 20 && white(rb_right, 44, 22, 54, 36) > 15 && white(rb_right, 6, 22, 20, 36) == 0);
    m.exit_deg = -90;
    render_turn_card(m, &rb_left);
    ok("roundabout, the exit left: round anticlockwise by the right, the head left, the quarter left of the way in grey",
       white(rb_left, 6, 22, 16, 36) > 15 && white(rb_left, 45, 14, 54, 44) == 0 && grey(rb_left, 30, 14, 54, 44) < 10 &&
           grey(rb_left, 6, 30, 30, 44) > 20);
    m.exit_deg = 180;
    render_turn_card(m, &rb_back);
    ok("roundabout, back: all but round, the head down on the left of the way in",
       white(rb_back, 6, 36, 22, 52) > 15 && rb_back != rb_left);
    m.exit_deg = 0;
    m.kind = Maneuver::Arrive;
    render_turn_card(m, &flag);
    ok("arrive: a chequered flag", white(flag, 23, 10, 43, 25) > 60 && grey(flag, 23, 10, 43, 25) > 60);
    m.kind = Maneuver::SharpRight;
    render_turn_card(m, &sharp);
    ok("sharp right: up the left, the head down on the right",
       white(sharp, 12, 30, 26, 50) > 40 && white(sharp, 36, 26, 54, 46) > 25);

    /* where traffic keeps left: a roundabout driven round clockwise; the
     * exit's number in the ring */
    {
        std::vector<uint8_t> acw, cw, cw_right, numbered, low, low_plain;
        Maneuver r;
        r.kind = Maneuver::Roundabout;
        r.distance = "220 yd";
        r.road = "Rua Exemplo";
        r.exit_deg = 90;
        render_turn_card(r, &acw);
        r.clockwise = true;
        r.exit_deg = -90;
        render_turn_card(r, &cw);
        ok("clockwise: the mirror of anticlockwise to the mirrored exit", icon_mirrored(acw, cw));
        ok("clockwise, the 1st exit left: the head on the left, round by the left (the ring's right grey)",
           white(cw, 6, 22, 16, 36) > 15 && white(cw, 44, 22, 54, 36) == 0 && grey(cw, 30, 14, 54, 44) > 20);
        r.exit_deg = 90;
        render_turn_card(r, &cw_right);
        ok("clockwise, an exit right: round by the left and over the top, the head on the right",
           white(cw_right, 44, 22, 54, 36) > 15 && white(cw_right, 6, 14, 22, 36) > 15);
        r.exit_number = 2;
        render_turn_card(r, &numbered);
        ok("the exit's number white in the ring's middle",
           white(numbered, 26, 22, 34, 36) > 12 && white(cw_right, 26, 22, 34, 36) < 3);
        bool text_same = true;
        for (unsigned y = 0; y < kTurnCardH; y++)
            for (unsigned x = 62; x < kTurnCardW; x++)
                text_same = text_same && !std::memcmp(at(numbered, x, y), at(cw_right, x, y), 4);
        ok("the number leaves the text as it was", text_same);
        r.exit_number = 12;
        std::vector<uint8_t> two;
        render_turn_card(r, &two);
        ok("two digits, smaller, still in the ring", white(two, 24, 22, 36, 36) > 12 && two != numbered);
        r.exit_number = 0;
        std::vector<uint8_t> none;
        render_turn_card(r, &none);
        ok("no number (0): an empty ring, the card as without one", none == cw_right && white(none, 26, 22, 34, 36) < 3);
        r.exit_number = 3;
        render_turn_card(r, &low, true);
        r.exit_number = 0;
        render_turn_card(r, &low_plain, true);
        ok("compact: the number in the smaller ring too, none for 0",
           white(low, 17, 14, 23, 21) > 4 && white(low_plain, 17, 14, 23, 21) < 2);
    }
    /* with a number in the ring, a neck between the ring and the exit's head
     * (>= 1 px full, 0.5 px compact) at every exit, both ways round; exit 0
     * an empty ring: no white within 5 units of its middle */
    {
        float neck = 1e9f;
        bool empty = true, digits = true;
        Maneuver r;
        r.kind = Maneuver::Roundabout;
        r.distance = "220 yd";
        r.road = "Rua Exemplo";
        for (int cw = 0; cw < 2; cw++)
            for (int a = -179; a <= 180; a++) {
                r.clockwise = cw == 1;
                r.exit_deg = a;
                for (int n : {1, 12}) {
                    r.exit_number = n;
                    const float k = roundabout_neck(r);
                    neck = k < neck ? k : neck;
                }
                for (int compact = 0; compact < 2; compact++) {
                    const float u = compact ? 28 / 48.0f : 1;
                    const float cx = 6 + 24 * u, cy = (compact ? 3 : 4) + 25 * u, rad = 5 * u;
                    std::vector<uint8_t> card;
                    for (int n : {0, 1}) {
                        r.exit_number = n;
                        render_turn_card(r, &card, compact == 1);
                        unsigned lit = 0;
                        for (int y = (int)(cy - rad); y <= (int)(cy + rad); y++)
                            for (int x = (int)(cx - rad); x <= (int)(cx + rad); x++) {
                                const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                                const uint8_t *p = at(card, x, y);
                                lit += dx * dx + dy * dy <= rad * rad && p[0] > 128;
                            }
                        if (n == 0)
                            empty = empty && lit == 0;
                        else
                            digits = digits && lit > 0;
                    }
                }
            }
        std::printf("     neck: %.2f px full, %.2f px compact\n", neck, neck * 28 / 48);
        ok("a number in the ring: a neck of 1 px or more (full), 0.5 (compact), every exit", neck >= 1.0f &&
               neck * 28 / 48 >= 0.5f);
        ok("exit 0: an empty ring at every exit, both ways round, both cards (1: digits there)", empty && digits);
    }
    /* the exit's arm and head clear of the ring's digits, and the digits
     * clear of the ring, both cards, every exit, both ways round: numbers 1
     * to 9 a whole pixel from the arm and not touching the ring; 10 to 19
     * not overlapping the arm. Two digits touch the ring (full and compact)
     * and come within half a pixel of the arm on the compact card: the
     * owner's accepted trade-off (2026-11-24) for the clean join. */
    {
        float arm[2][2], ring[2][2];   /* [1 to 9, 10 to 19][full, compact] */
        for (auto &g : arm)
            g[0] = g[1] = 1e9f;
        for (auto &g : ring)
            g[0] = g[1] = 1e9f;
        Maneuver r;
        r.kind = Maneuver::Roundabout;
        for (int cw = 0; cw < 2; cw++)
            for (int a = -179; a <= 180; a++)
                for (int n = 1; n <= 19; n++) {
                    r.clockwise = cw == 1;
                    r.exit_deg = a;
                    r.exit_number = n;
                    for (int compact = 0; compact < 2; compact++) {
                        float &g = arm[n > 9][compact], &h = ring[n > 9][compact];
                        g = std::fmin(g, roundabout_digit_clearance(r, compact == 1));
                        h = std::fmin(h, roundabout_ring_gap(r, compact == 1));
                    }
                }
        std::printf("     digits to the arm: 1-9 %.2f / %.2f px, 10-19 %.2f / %.2f px (full / compact)\n", arm[0][0],
                    arm[0][1], arm[1][0], arm[1][1]);
        std::printf("     digits to the ring: 1-9 %.2f / %.2f px, 10-19 %.2f / %.2f px (full / compact)\n", ring[0][0],
                    ring[0][1], ring[1][0], ring[1][1]);
        ok("the arm and head 1 px or more clear of 1 to 9, full and compact", arm[0][0] >= 1 && arm[0][1] >= 1);
        ok("the arm and head not over 10 to 19, full and compact", arm[1][0] >= 0 && arm[1][1] >= 0);
        ok("1 to 9 not over the ring, full and compact", ring[0][0] >= 0 && ring[0][1] >= 0);
    }
    /* a numbered ring: the stem and the arm join its band cleanly, nothing
     * of either in its hole, nothing outside it wider than their own width,
     * no background where the arm meets the ring, both cards, every exit,
     * both ways round */
    {
        int faults = 0;
        Maneuver r;
        r.kind = Maneuver::Roundabout;
        for (int cw = 0; cw < 2; cw++)
            for (int a = -179; a <= 180; a++)
                for (int n : {1, 12}) {
                    r.clockwise = cw == 1;
                    r.exit_deg = a;
                    r.exit_number = n;
                    faults += roundabout_join_faults(r, false) + roundabout_join_faults(r, true);
                }
        ok("a numbered ring: no stem or arm in its hole, no bulge beside them, no notch", faults == 0);
    }
    {
        std::vector<uint8_t> boat, low;
        Maneuver f;
        f.kind = Maneuver::Ferry;
        f.distance = "2.5 mi";
        f.road = "Take the ferry";
        render_turn_card(f, &boat);
        ok("a ferry: its boat in the icon's square", white(boat, 6, 4, 54, 52) > 150);
        Maneuver o = f;
        o.kind = Maneuver::Other;
        std::vector<uint8_t> other;
        render_turn_card(o, &other);
        bool differs = false;
        for (unsigned y = 4; y < 52 && !differs; y++)
            for (unsigned x = 6; x < 54 && !differs; x++)
                differs = std::memcmp(at(boat, x, y), at(other, x, y), 4) != 0;
        ok("a ferry: not the plain mark", differs);
        render_turn_card(f, &low, true);
        ok("compact: the ferry smaller", white(low, 6, 3, 34, 31) > 50);
    }

    /* the compact card below the arrow: one line */
    m.kind = Maneuver::Left;
    m.distance = "200 m";
    m.road = "Rua Exemplo";
    std::vector<uint8_t> low, low_none;
    render_turn_card(m, &low, true);
    ok("compact: kTurnCardW x kTurnCardCompactH", low.size() == (size_t)kTurnCardW * kTurnCardCompactH * 4);
    ok("compact: the arrow smaller, in its left square",
       white(low, 6, 3, 34, 31) > 60 && white(low, 0, 0, 6, kTurnCardCompactH) == 0);
    ok("compact: the distance in bold right of the arrow", white(low, 42, 4, 120, 27) > 60);
    ok("compact: the road after the distance, on the same line", light(low, 125, 8, 348, 30) > 40);
    ok("compact: nothing written below the line", white(low, 36, 31, kTurnCardW, kTurnCardCompactH) == 0 &&
                                                        light(low, 36, 31, kTurnCardW, kTurnCardCompactH) == 0);
    m.distance.clear();
    std::vector<uint8_t> low_road;
    render_turn_card(m, &low_road, true);
    ok("compact: no distance, the road where the distance would start", light(low_road, 42, 8, 120, 30) > 40);
    m.kind = Maneuver::None;
    render_turn_card(m, &low_none, true);
    bool clear = low_none.size() == (size_t)kTurnCardW * kTurnCardCompactH * 4;
    for (uint8_t b : low_none)
        clear = clear && b == 0;
    ok("compact: no manoeuvre, no card", clear);

    /* the app's example over the navigation's, and its end (card_choice.h) */
    {
        CardChoice ch;
        Maneuver g, ex;
        g.kind = Maneuver::Right;
        g.distance = "300 m";
        ex.kind = Maneuver::Left;
        ex.distance = "200 m";
        ch.set_guided(g);
        ok("choice: the navigation's manoeuvre", ch.shown() == g && !ch.example());
        ch.set_example(ex, 1000);
        ok("choice: the app's example over it", ch.shown() == ex && ch.example());
        ok("choice: the example lasts its time", !ch.expire(1000 + CardChoice::kExampleMs - 1) && ch.shown() == ex);
        ok("choice: then ends on its own, the app's end or not, and the navigation's shows again",
           ch.expire(1000 + CardChoice::kExampleMs) && ch.shown() == g && !ch.expire(1000 + CardChoice::kExampleMs + 5000));
        ch.set_example(ex, 50000);
        ch.set_example(ex, 55000);
        ok("choice: another example starts its time again", !ch.expire(50000 + CardChoice::kExampleMs) && ch.example());
        ch.set_example(Maneuver(), 56000);
        ok("choice: the app's none ends it at once", !ch.example() && ch.shown() == g && !ch.expire(70000));
        g.distance = "100 m";
        ch.set_guided(g);
        ok("choice: the navigation's next one", ch.shown().distance == "100 m");
    }

    const std::string path = "/tmp/woc-turn-card-test-" + std::to_string(getpid()) + ".conf";
    unlink(path.c_str());
    ok("default: off", TurnCardSetting("off", path).mode() == TurnCardSetting::Off);
    ok("the configuration's bottom and top",
       TurnCardSetting("bottom", path).mode() == TurnCardSetting::Bottom &&
           TurnCardSetting("top", path).mode() == TurnCardSetting::Top);
    ok("the configuration's other values: off", TurnCardSetting("1", path).mode() == TurnCardSetting::Off);
    {
        TurnCardSetting s("off", path);
        ok("the app's bottom", s.set("bottom") && s.mode() == TurnCardSetting::Bottom &&
                                   std::string(s.name()) == "bottom");
        ok("another choice: refused, nothing changed", !s.set("middle") && !s.set("") &&
                                                           s.mode() == TurnCardSetting::Bottom);
    }
    ok("after a restart: still bottom, over the configuration's off",
       TurnCardSetting("off", path).mode() == TurnCardSetting::Bottom);
    {
        TurnCardSetting s("bottom", path);
        ok("the app's off", s.set("off") && s.mode() == TurnCardSetting::Off);
    }
    ok("after a restart: off, over the configuration's top", TurnCardSetting("top", path).mode() == TurnCardSetting::Off);
    std::ofstream(path) << "turn_card = sideways\n";
    ok("a saved value that is none of them: off", TurnCardSetting("top", path).mode() == TurnCardSetting::Off);
    std::ofstream(path) << "garbage\n";
    ok("a state file without the setting: off", TurnCardSetting("top", path).mode() == TurnCardSetting::Off);
    unlink(path.c_str());

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
