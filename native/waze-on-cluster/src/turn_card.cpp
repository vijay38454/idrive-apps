// SPDX-License-Identifier: GPL-3.0-or-later
#include "turn_card.h"

#include <cmath>
#include <cstring>

#include "card_font.h"

namespace woc {

namespace {

const float kPi = 3.14159265f;

struct Canvas {
    std::vector<uint8_t> *px;
    unsigned h;   /* rows: kTurnCardH, or kTurnCardCompactH */

    /* colour over what is there, at coverage a (0-1). */
    void blend(int x, int y, uint8_t r, uint8_t g, uint8_t b, float a)
    {
        if (x < 0 || y < 0 || x >= (int)kTurnCardW || y >= (int)h || a <= 0)
            return;
        if (a > 1)
            a = 1;
        uint8_t *p = &(*px)[((size_t)y * kTurnCardW + x) * 4];
        const float da = p[3] / 255.0f, oa = a + da * (1 - a);
        const uint8_t c[3] = {r, g, b};
        for (int i = 0; i < 3; i++)
            p[i] = (uint8_t)std::lround((c[i] * a + p[i] * da * (1 - a)) / oa);
        p[3] = (uint8_t)std::lround(oa * 255);
    }
};

struct Pt {
    float x, y;
};

float dist(Pt a, Pt b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

float seg_dist(float px, float py, Pt a, Pt b)
{
    const float vx = b.x - a.x, vy = b.y - a.y, wx = px - a.x, wy = py - a.y;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0 ? (wx * vx + wy * vy) / len2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const float dx = px - (a.x + t * vx), dy = py - (a.y + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

/* Coverage of an edge at signed distance d inside (positive) it. */
float edge(float d)
{
    return d <= -0.5f ? 0 : d >= 0.5f ? 1 : d + 0.5f;
}

/* A compass bearing (degrees clockwise from up) as a unit vector, y down. */
Pt bearing(float deg)
{
    return {std::sin(deg * kPi / 180), -std::cos(deg * kPi / 180)};
}

/* An icon in a 48x48 square, as for traffic on the right: strokes (round
 * capped), arrow heads, arcs, rectangles and discs, each white (the way to
 * go) or grey (a road not taken). */
struct Icon {
    struct Stroke {
        std::vector<Pt> pts;
        float half;
        bool grey;
        float clip = 0;      /* no ink nearer clip_c than this */
        Pt clip_c = {0, 0};
    };
    struct Head {
        Pt tip, a, b;
    };
    struct Arc {
        Pt c;
        float r, half;
        float from, to;   /* bearings: the arc covers from..to clockwise */
        bool grey;
        float cut = 0;      /* > 0: no ink farther than this along cut_n from c */
        Pt cut_n = {0, 0};
    };
    struct Box {
        float x0, y0, x1, y1;
        bool grey;
    };
    struct Disc {
        Pt c;
        float r;
    };
    std::vector<Stroke> strokes;
    std::vector<Head> heads;
    std::vector<Arc> arcs;
    std::vector<Box> boxes;
    std::vector<Disc> discs;
};

const float kHalf = 3.7f;      /* half the shaft's width */
const float kHeadLen = 12.5f;  /* base to tip */
const float kHeadHalf = 9.5f;  /* half the head's base */

/* pts as one path, each corner rounded into a curve of radius r (at most
 * half of either side). */
std::vector<Pt> rounded(const std::vector<Pt> &p, float r)
{
    std::vector<Pt> out;
    if (p.empty())
        return out;
    out.push_back(p[0]);
    for (size_t i = 1; i + 1 < p.size(); i++) {
        const Pt a = p[i - 1], b = p[i], c = p[i + 1];
        const float l1 = dist(a, b), l2 = dist(b, c);
        if (l1 <= 0 || l2 <= 0)
            continue;
        const float r1 = r < l1 / 2 ? r : l1 / 2, r2 = r < l2 / 2 ? r : l2 / 2;
        const Pt s = {b.x - (b.x - a.x) / l1 * r1, b.y - (b.y - a.y) / l1 * r1};
        const Pt e = {b.x + (c.x - b.x) / l2 * r2, b.y + (c.y - b.y) / l2 * r2};
        for (int k = 0; k <= 10; k++) {
            const float t = k / 10.0f, u = 1 - t;
            out.push_back({u * u * s.x + 2 * u * t * b.x + t * t * e.x, u * u * s.y + 2 * u * t * b.y + t * t * e.y});
        }
    }
    out.push_back(p.back());
    return out;
}

/* A white shaft along pts (corners rounded by r) and a head past its end. */
void arrow(Icon *ic, const std::vector<Pt> &pts, float r)
{
    const std::vector<Pt> path = rounded(pts, r);
    ic->strokes.push_back({path, kHalf, false});
    const Pt b = path.back(), a = path[path.size() - 2];
    const float l = dist(a, b);
    const Pt u = {(b.x - a.x) / l, (b.y - a.y) / l};
    ic->heads.push_back({{b.x + u.x * kHeadLen, b.y + u.y * kHeadLen},
                         {b.x - u.y * kHeadHalf, b.y + u.x * kHeadHalf},
                         {b.x + u.y * kHeadHalf, b.y - u.x * kHeadHalf}});
}

/* A roundabout's exit as drawn, -135 to 128: the exit kept clear of the way
 * in, the sharpest right at 128 (its head clear of the way in), back (anticlockwise, all but round) down
 * to the left of it. */
float drawn_exit(int exit_deg)
{
    float e = (float)exit_deg;
    if (e > 180 || e <= -180)
        e = std::fmod(std::fmod(e + 180, 360.0f) + 360, 360.0f) - 180;
    if (e > 128)
        e = e >= 165 ? -135 : 128;
    if (e < -135)
        e = -135;
    return e;
}

/* The ring's middle for exit e: (24, 25); with the exit's number in it (a
 * wider ring) moved 2.8 back from the exit, to leave room for a neck
 * between the ring and the exit's head. */
Pt ring_centre(float e, bool numbered)
{
    if (!numbered)
        return {24, 25};
    const Pt u = bearing(e);
    return {24 - u.x * 2.8f, 25 - u.y * 2.8f};
}

/* The roundabout: the ring grey, the way round it white from its foot
 * anticlockwise to the exit, the exit's arm and head along bearing exit.
 * numbered: the ring wider (12) for the exit's number inside it, moved back
 * from the exit (ring_centre), a neck of 1.5 between the ring's edge and the
 * head; the stem runs straight across the ring's band to its inner edge, the
 * arm into the band from its round end, cut at the band's centre line as
 * wide as the way round's own end there (no step between them, and clear of
 * the digits), nothing of either in the hole or beside its own width. Otherwise as it always was. Clockwise is
 * its mirror (icon_of). */
void roundabout(Icon *ic, int exit_deg, bool numbered)
{
    const float e = drawn_exit(exit_deg);
    const Pt c = ring_centre(e, numbered);
    const float r = numbered ? 12 : 10, ring = 2.6f, arm = numbered ? 4.1f : 3;
    /* numbered: the stem from the middle, cut at the band's inner edge; the
     * arm's round end cut at the centre line where it is as wide as the
     * ring's band, the way round carried past the exit to the arm's far
     * edge (below) */
    const float clip = numbered ? r - ring : 0;
    const float start = numbered ? r + std::sqrt(kHalf * kHalf - ring * ring) : r;
    ic->arcs.push_back({c, r, ring, 0, 360, true});
    ic->strokes.push_back({{{c.x, 47}, {c.x, c.y + (numbered ? 0 : r)}}, kHalf, false, clip, c});
    /* anticlockwise from the foot (bearing 180) down to e */
    ic->arcs.push_back({c, r, ring, e, 180, false});
    /* numbered: the arm's far edge one straight line through the band to
     * its inner edge, the band white up to it (no bump, no step) */
    if (numbered)
        ic->arcs.push_back({c, r, ring, e - 30, e, false, kHalf, bearing(e - 90)});
    const Pt u = bearing(e);
    const Pt s = {c.x + u.x * start, c.y + u.y * start}, b = {c.x + u.x * (r + arm), c.y + u.y * (r + arm)};
    ic->strokes.push_back({{s, b}, kHalf, false, numbered ? r : 0, c});
    ic->heads.push_back({{b.x + u.x * (kHeadLen - 2), b.y + u.y * (kHeadLen - 2)},
                         {b.x - u.y * (kHeadHalf - 1), b.y + u.x * (kHeadHalf - 1)},
                         {b.x + u.y * (kHeadHalf - 1), b.y - u.x * (kHeadHalf - 1)}});
}

/* A ferry: a boat (its hull, cabin and funnel) white over grey waves. */
void ferry(Icon *ic)
{
    /* the hull, narrower at the keel: split along both diagonals, each
     * inside a triangle, so no seam shows */
    ic->heads.push_back({{9, 26}, {41, 26}, {35, 35}});
    ic->heads.push_back({{9, 26}, {35, 35}, {15, 35}});
    ic->heads.push_back({{9, 26}, {41, 26}, {15, 35}});
    ic->heads.push_back({{41, 26}, {35, 35}, {15, 35}});
    ic->boxes.push_back({17, 17, 33, 26, false});   /* the cabin */
    ic->boxes.push_back({22, 9, 27, 17, false});    /* the funnel */
    ic->strokes.push_back({{{6, 42}, {12, 39}, {18, 42}, {24, 39}, {30, 42}, {36, 39}, {42, 42}}, 1.8f, true});
}

/* The destination: a chequered flag on its pole, the pole at x. */
void flag(Icon *ic, float x)
{
    ic->strokes.push_back({{{x, 46}, {x, 6}}, 2.0f, false});
    const float x0 = x + 2, y0 = 6, cw = 5, ch = 5;
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 4; col++)
            ic->boxes.push_back({x0 + col * cw, y0 + row * ch, x0 + (col + 1) * cw, y0 + (row + 1) * ch,
                                 (row + col) % 2 == 1});
}

/* Whether m's roundabout shows its exit's number in the ring: 1 to 19. */
bool ring_number(const Maneuver &m)
{
    return m.kind == Maneuver::Roundabout && m.exit_number >= 1 && m.exit_number <= 19;
}

/* The icon of m as for traffic on the right, and whether it is drawn
 * mirrored (a left-hand kind: its right-hand one; a roundabout driven round
 * clockwise: the anticlockwise one to the mirrored exit). */
Icon icon_of(const Maneuver &m, bool *mirror)
{
    Icon ic;
    *mirror = false;
    switch (m.kind) {
    case Maneuver::SlightLeft:
    case Maneuver::Left:
    case Maneuver::SharpLeft:
    case Maneuver::KeepLeft:
    case Maneuver::ExitLeft:
    case Maneuver::UTurnRight:
    case Maneuver::ArriveLeft:
        *mirror = true;
        break;
    default:
        break;
    }
    switch (m.kind) {
    case Maneuver::Straight: arrow(&ic, {{24, 47}, {24, 15}}, 0); break;
    case Maneuver::SlightLeft:
    case Maneuver::SlightRight: arrow(&ic, {{16, 47}, {16, 31}, {27, 20}}, 9); break;
    case Maneuver::Left:
    case Maneuver::Right: arrow(&ic, {{12.5f, 47}, {12.5f, 23}, {26.5f, 23}}, 9); break;
    case Maneuver::SharpLeft:
    case Maneuver::SharpRight: arrow(&ic, {{13, 47}, {13, 13}, {29, 27}}, 10); break;
    /* back over the left; mirrored, over the right */
    case Maneuver::UTurn:
    case Maneuver::UTurnRight: arrow(&ic, {{35.5f, 47}, {35.5f, 12}, {18.5f, 12}, {18.5f, 27}}, 8); break;
    case Maneuver::KeepLeft:
    case Maneuver::KeepRight:
        ic.strokes.push_back({rounded({{20, 47}, {20, 33}, {14, 22}, {14, -4}}, 7), kHalf, true});
        arrow(&ic, {{20, 47}, {20, 33}, {30, 23}, {30, 15}}, 8);
        break;
    case Maneuver::ExitLeft:
    case Maneuver::ExitRight:
        ic.strokes.push_back({{{19, 47}, {19, 3}}, kHalf, true});
        arrow(&ic, {{19, 47}, {19, 30}, {29, 20}}, 9);
        break;
    case Maneuver::Roundabout:
        *mirror = m.clockwise;
        roundabout(&ic, m.clockwise ? -m.exit_deg : m.exit_deg, ring_number(m));
        break;
    case Maneuver::Ferry: ferry(&ic); break;
    case Maneuver::Arrive: flag(&ic, 14); break;
    /* the flag at the road's side: mirrored, on the left */
    case Maneuver::ArriveLeft:
    case Maneuver::ArriveRight:
        ic.strokes.push_back({{{9, 47}, {9, -4}}, kHalf, true});
        flag(&ic, 21);
        break;
    case Maneuver::Other:
        /* no direction to tell: a dot in the middle, no stroke */
        ic.discs.push_back({{24, 24}, 7});
        break;
    case Maneuver::None: break;
    }
    return ic;
}

/* inside distance of p to a triangle (positive inside) */
float tri_in(float fx, float fy, const Pt t[3])
{
    /* the winding decides which side is in */
    const float area = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x);
    float in = 1e9f;
    for (int i = 0; i < 3; i++) {
        const Pt p = t[i], q = t[(i + 1) % 3];
        const float ex = q.x - p.x, ey = q.y - p.y, el = std::sqrt(ex * ex + ey * ey);
        float s = ((fx - p.x) * ey - (fy - p.y) * ex) / el;
        if (area > 0)
            s = -s;
        in = s < in ? s : in;
    }
    return in;
}

/* the arc's signed inside distance at (fx, fy), in the icon's units: the
 * ring's band within from..to, round capped at both ends */
float arc_in(float fx, float fy, const Icon::Arc &a)
{
    const float dx = fx - a.c.x, dy = fy - a.c.y;
    float b = std::atan2(dx, -dy) * 180 / kPi;   /* bearing of the pixel */
    float span = a.to - a.from;
    if (span >= 360)
        return a.half - std::fabs(std::sqrt(dx * dx + dy * dy) - a.r);
    while (b < a.from)
        b += 360;
    while (b >= a.from + 360)
        b -= 360;
    if (b <= a.from + span)
        return a.half - std::fabs(std::sqrt(dx * dx + dy * dy) - a.r);
    const Pt p0 = {a.c.x + bearing(a.from).x * a.r, a.c.y + bearing(a.from).y * a.r};
    const Pt p1 = {a.c.x + bearing(a.to).x * a.r, a.c.y + bearing(a.to).y * a.r};
    const float d0 = dist({fx, fy}, p0), d1 = dist({fx, fy}, p1);
    return a.half - (d0 < d1 ? d0 : d1);
}

/* m's icon, drawn size x size pixels (48: the icon's own units) at ox, oy:
 * each pixel's centre taken back to the 48x48 drawing, and each edge's
 * distance to pixels again, so a smaller icon stays as smooth. */
void draw_shapes(Canvas *c, const Icon &ic, bool mirror, int ox, int oy, int size)
{
    const float k = size / 48.0f;   /* pixels per unit of the drawing */
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            /* the pixel's centre, in the right-hand drawing */
            const float ux = (x + 0.5f) / k, fy = (y + 0.5f) / k;
            const float fx = mirror ? 48 - ux : ux;
            float white = 0, grey = 0;
            for (const Icon::Stroke &s : ic.strokes) {
                float d = 1e9f;
                for (size_t i = 0; i + 1 < s.pts.size(); i++) {
                    const float sd = seg_dist(fx, fy, s.pts[i], s.pts[i + 1]);
                    d = sd < d ? sd : d;
                }
                float in = s.half - d;
                if (s.clip > 0) {
                    const float out = dist({fx, fy}, s.clip_c) - s.clip;
                    in = out < in ? out : in;
                }
                const float cov = edge(in * k);
                float &dst = s.grey ? grey : white;
                dst = cov > dst ? cov : dst;
            }
            for (const Icon::Head &h : ic.heads) {
                const Pt t[3] = {h.tip, h.a, h.b};
                const float cov = edge(tri_in(fx, fy, t) * k);
                white = cov > white ? cov : white;
            }
            for (const Icon::Arc &a : ic.arcs) {
                float in = arc_in(fx, fy, a);
                if (a.cut > 0) {
                    const float out = a.cut - ((fx - a.c.x) * a.cut_n.x + (fy - a.c.y) * a.cut_n.y);
                    in = out < in ? out : in;
                }
                const float cov = edge(in * k);
                float &dst = a.grey ? grey : white;
                dst = cov > dst ? cov : dst;
            }
            for (const Icon::Box &b : ic.boxes) {
                float in = fx - b.x0;
                in = b.x1 - fx < in ? b.x1 - fx : in;
                in = fy - b.y0 < in ? fy - b.y0 : in;
                in = b.y1 - fy < in ? b.y1 - fy : in;
                const float cov = edge(in * k);
                float &dst = b.grey ? grey : white;
                dst = cov > dst ? cov : dst;
            }
            for (const Icon::Disc &d : ic.discs) {
                const float cov = edge((d.r - dist({fx, fy}, d.c)) * k);
                white = cov > white ? cov : white;
            }
            /* grey under white: a road not taken shows only where the way
             * to go does not */
            if (grey > 0)
                c->blend(ox + x, oy + y, 118, 126, 140, grey);
            if (white > 0)
                c->blend(ox + x, oy + y, 255, 255, 255, white);
        }
    }
}

void draw_icon(Canvas *c, const Maneuver &m, int ox, int oy, int size)
{
    bool mirror = false;
    /* the icon first: icon_of sets mirror, and a call's arguments are
     * evaluated in no set order (g++ from the right) */
    const Icon ic = icon_of(m, &mirror);
    draw_shapes(c, ic, mirror, ox, oy, size);
}

/* runs of f with the pen at x, the baseline at y */
void draw_line(Canvas *c, const CardFont &f, const std::vector<CardRun> &runs, int x, int y, uint8_t r, uint8_t g,
               uint8_t b)
{
    for (const CardRun &run : runs) {
        const CardGlyph &gl = *run.g;
        const int gx = x + (run.x16 + 8) / 16 + gl.x, gy = y + gl.y;
        for (int py = 0; py < gl.h; py++)
            for (int px = 0; px < gl.w; px++) {
                const uint8_t a = card_glyph_px(f, gl, px, py);
                if (a)
                    c->blend(gx + px, gy + py, r, g, b, a / 255.0f);
            }
    }
}

/* The small bold text of the ring's number: the distance's bold font at
 * kSmall of its size, each pixel the mean of 4x4 samples of the
 * glyphs' pictures. */
const float kSmall = 0.6f;

/* runs of f at k of their size, the pen at x, the baseline at base */
void draw_scaled(Canvas *c, const CardFont &f, const std::vector<CardRun> &runs, float x, float base, float k,
                 uint8_t r, uint8_t g, uint8_t b)
{
    for (const CardRun &run : runs) {
        const CardGlyph &gl = *run.g;
        if (!gl.w || !gl.h)
            continue;
        const float pen = x + run.x16 * k / 16.0f;
        /* the glyph's coverage of pixel px, py with its picture's top-left at
         * x0, y0, in 0..16*255 */
        auto cover = [&](float x0, float y0, int px, int py) {
            int sum = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    const int gx = (int)std::floor((px + (sx + 0.5f) / 4 - x0) / k);
                    const int gy = (int)std::floor((py + (sy + 0.5f) / 4 - y0) / k);
                    if (gx >= 0 && gy >= 0 && gx < gl.w && gy < gl.h)
                        sum += card_glyph_px(f, gl, gx, gy);
                }
            return sum;
        };
        /* at a few pixels tall a stroke is about one pixel wide: where it
         * falls across two it is grey in both. So the glyph moves by less
         * than half a pixel (eighths) to where its strokes land on whole
         * pixels: the most full ones, then the most contrast, then the
         * smallest move */
        const float tx = pen + gl.x * k, ty = base + gl.y * k;
        float x0 = tx, y0 = ty;
        int best_full = -1;
        double best_sq = -1;
        float best_move = 1e9f;
        for (int j = -4; j < 4; j++)
            for (int i = -4; i < 4; i++) {
                const float cx0 = tx + i / 8.0f, cy0 = ty + j / 8.0f, move = (float)(i * i + j * j);
                int full = 0;
                double sq = 0;
                for (int py = (int)std::floor(cy0); py < (int)std::ceil(cy0 + gl.h * k); py++)
                    for (int px = (int)std::floor(cx0); px < (int)std::ceil(cx0 + gl.w * k); px++) {
                        const int s = cover(cx0, cy0, px, py);
                        full += s * 255 > 240 * 16 * 255;
                        sq += (double)s * s;
                    }
                if (full > best_full || (full == best_full && (sq > best_sq || (sq == best_sq && move < best_move)))) {
                    best_full = full;
                    best_sq = sq;
                    best_move = move;
                    x0 = cx0;
                    y0 = cy0;
                }
            }
        for (int py = (int)std::floor(y0); py < (int)std::ceil(y0 + gl.h * k); py++)
            for (int px = (int)std::floor(x0); px < (int)std::ceil(x0 + gl.w * k); px++) {
                const int sum = cover(x0, y0, px, py);
                if (sum)
                    c->blend(px, py, r, g, b, sum / (16 * 255.0f));
            }
    }
}

/* The exit's number in the ring of m's icon, drawn size px at ox, oy: small
 * bold white digits (two narrower), centred on the ring's middle. */
void draw_ring_number(Canvas *c, const Maneuver &m, int ox, int oy, int size)
{
    const float u = size / 48.0f;   /* pixels per unit of the icon */
    const std::string n = std::to_string(m.exit_number);
    /* the digits' height: 12 units, 10 for two */
    const CardGlyph *zero = card_glyph(card_font_distance(), '0');
    const float h = (n.size() > 1 ? 10.0f : 12.0f) * u, k = zero ? h / zero->h : kSmall;
    int w = 0;
    const std::vector<CardRun> runs = card_line(card_font_distance(), n, 999, &w);
    /* on the ring's middle (ring_centre), mirrored for a clockwise one */
    const Pt rc = ring_centre(drawn_exit(m.clockwise ? -m.exit_deg : m.exit_deg), true);
    const float cx = ox + (m.clockwise ? 48 - rc.x : rc.x) * u, cy = oy + rc.y * u;
    /* the pen and baseline on whole pixels: the same digits, as sharp
     * (draw_scaled), wherever the ring's middle falls */
    draw_scaled(c, card_font_distance(), runs, std::floor(cx - w * k / 2), std::floor(cy + h / 2), k, 255, 255, 255);
}

}  // namespace

bool maneuver_kind(const std::string &name, Maneuver::Kind *out)
{
    static const struct {
        const char *name;
        Maneuver::Kind kind;
    } kinds[] = {{"none", Maneuver::None},
                 {"straight", Maneuver::Straight},
                 {"slight_left", Maneuver::SlightLeft},
                 {"left", Maneuver::Left},
                 {"sharp_left", Maneuver::SharpLeft},
                 {"slight_right", Maneuver::SlightRight},
                 {"right", Maneuver::Right},
                 {"sharp_right", Maneuver::SharpRight},
                 {"uturn", Maneuver::UTurn},
                 {"uturn_right", Maneuver::UTurnRight},
                 {"keep_left", Maneuver::KeepLeft},
                 {"keep_right", Maneuver::KeepRight},
                 {"exit_left", Maneuver::ExitLeft},
                 {"exit_right", Maneuver::ExitRight},
                 {"roundabout", Maneuver::Roundabout},
                 {"ferry", Maneuver::Ferry},
                 {"arrive", Maneuver::Arrive},
                 {"arrive_left", Maneuver::ArriveLeft},
                 {"arrive_right", Maneuver::ArriveRight},
                 {"other", Maneuver::Other}};
    for (const auto &k : kinds)
        if (name == k.name) {
            *out = k.kind;
            return true;
        }
    return false;
}

void render_turn_card(const Maneuver &m, std::vector<uint8_t> *rgba, bool compact)
{
    const unsigned h = compact ? kTurnCardCompactH : kTurnCardH;
    rgba->assign((size_t)kTurnCardW * h * 4, 0);
    if (m.kind == Maneuver::None)
        return;
    Canvas c{rgba, h};
    /* the plate: dark, a little see-through, its corners rounded */
    const float rad = compact ? 10 : 12;
    for (int y = 0; y < (int)h; y++)
        for (int x = 0; x < (int)kTurnCardW; x++) {
            const float fx = x + 0.5f, fy = y + 0.5f;
            const float cx = fx < rad ? rad : fx > kTurnCardW - rad ? kTurnCardW - rad : fx;
            const float cy = fy < rad ? rad : fy > h - rad ? h - rad : fy;
            const float d = std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy));
            c.blend(x, y, 16, 20, 28, edge(rad - d) * 0.88f);
        }
    const CardFont &df = card_font_distance(), &rf = card_font_road();
    if (compact) {
        /* one line: the arrow smaller, then the distance in bold and the
         * road after it, on one baseline */
        draw_icon(&c, m, 6, 3, 28);
        if (ring_number(m))
            draw_ring_number(&c, m, 6, 3, 28);
        const int tx = 42, base = 25, gap = 10;
        int dw = 0;
        const std::vector<CardRun> dist = card_line(df, m.distance, (int)kTurnCardW - tx - 12, &dw);
        draw_line(&c, df, dist, tx, base, 255, 255, 255);
        const int rx = dist.empty() ? tx : tx + dw + gap;
        const std::vector<CardRun> road = card_line(rf, m.road, (int)kTurnCardW - rx - 12, nullptr);
        draw_line(&c, rf, road, rx, base, 200, 208, 220);
        return;
    }
    draw_icon(&c, m, 6, 4, 48);
    if (ring_number(m))
        draw_ring_number(&c, m, 6, 4, 48);
    /* the text right of the arrow: the distance in bold on top, the road
     * below; the distance alone in the middle */
    const int tx = 62, room = (int)kTurnCardW - tx - 12;
    const std::vector<CardRun> dist = card_line(df, m.distance, room, nullptr);
    const std::vector<CardRun> road = card_line(rf, m.road, room, nullptr);
    if (road.empty()) {
        draw_line(&c, df, dist, tx, 37, 255, 255, 255);
    } else {
        draw_line(&c, df, dist, tx, 27, 255, 255, 255);
        draw_line(&c, rf, road, tx, 48, 200, 208, 220);
    }
}

float roundabout_neck(const Maneuver &m)
{
    bool mirror = false;
    const Icon ic = icon_of(m, &mirror);
    if (m.kind != Maneuver::Roundabout || ic.arcs.empty() || ic.heads.empty())
        return 0;
    const Icon::Arc &ring = ic.arcs[0];
    const Icon::Head &h = ic.heads[0];
    const Pt base = {(h.a.x + h.b.x) / 2, (h.a.y + h.b.y) / 2};
    return dist(base, ring.c) - (ring.r + ring.half);
}

/* The whole pixels between part of m's icon and the digits in its ring. */
static float digit_gap(const Maneuver &m, const Icon &part, bool mirror, bool compact)
{
    const int size = compact ? 28 : 48;
    std::vector<uint8_t> a((size_t)kTurnCardW * kTurnCardH * 4, 0), d = a;
    Canvas ca{&a, kTurnCardH}, cd{&d, kTurnCardH};
    draw_shapes(&ca, part, mirror, 0, 0, size);
    draw_ring_number(&cd, m, 0, 0, size);
    std::vector<int> ia, id;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const size_t i = ((size_t)y * kTurnCardW + x) * 4 + 3;
            if (a[i] >= 64)
                ia.push_back(y * size + x);
            if (d[i] >= 64)
                id.push_back(y * size + x);
        }
    float best = 1e9f;
    for (int p : ia)
        for (int q : id) {
            const float dx = (float)(p % size - q % size), dy = (float)(p / size - q / size);
            best = std::fmin(best, std::sqrt(dx * dx + dy * dy) - 1);
        }
    return best;
}

float roundabout_digit_clearance(const Maneuver &m, bool compact)
{
    if (!ring_number(m))
        return 1e9f;
    bool mirror = false;
    const Icon ic = icon_of(m, &mirror);
    Icon arm;
    arm.strokes = {ic.strokes[1]};
    arm.heads = ic.heads;
    return digit_gap(m, arm, mirror, compact);
}

float roundabout_ring_gap(const Maneuver &m, bool compact)
{
    if (!ring_number(m))
        return 1e9f;
    bool mirror = false;
    const Icon ic = icon_of(m, &mirror);
    Icon ring;
    ring.arcs = {ic.arcs[0]};
    return digit_gap(m, ring, mirror, compact);
}

/* A numbered roundabout's pixels at a fault: the stem's or the arm's ink
 * inside the ring's inner edge; white (heads aside) outside its outer edge
 * and beyond the stem's and the arm's own widths; or background in the
 * ring's band or where the arm leaves it. Half a pixel of give. */
int roundabout_join_faults(const Maneuver &m, bool compact)
{
    if (!ring_number(m))
        return 0;
    const int size = compact ? 28 : 48;
    const float k = size / 48.0f, give = 0.5f / k;
    bool mirror = false;
    const Icon ic = icon_of(m, &mirror);
    const Icon::Arc &ring = ic.arcs[0];
    const Pt c = ring.c, u = bearing(drawn_exit(m.clockwise ? -m.exit_deg : m.exit_deg));
    Icon straight, white;
    straight.strokes = ic.strokes;
    white.strokes = ic.strokes;
    for (const Icon::Arc &a : ic.arcs)
        if (!a.grey)
            white.arcs.push_back(a);
    std::vector<uint8_t> a((size_t)kTurnCardW * kTurnCardH * 4, 0), w = a, all = a;
    Canvas ca{&a, kTurnCardH}, cw{&w, kTurnCardH}, cl{&all, kTurnCardH};
    draw_shapes(&ca, straight, mirror, 0, 0, size);
    draw_shapes(&cw, white, mirror, 0, 0, size);
    draw_shapes(&cl, ic, mirror, 0, 0, size);
    int faults = 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const size_t i = ((size_t)y * kTurnCardW + x) * 4 + 3;
            const float ux = (x + 0.5f) / k, fy = (y + 0.5f) / k, fx = mirror ? 48 - ux : ux;
            const float d = dist({fx, fy}, c);
            if (a[i] >= 64 && d < ring.r - ring.half - give)
                faults++;
            const float along = (fx - c.x) * u.x + (fy - c.y) * u.y;
            const float side = std::fabs((fx - c.x) * u.y - (fy - c.y) * u.x);
            const bool in_arm = along > 0 && side <= kHalf + give;
            const bool in_stem = fy > c.y && std::fabs(fx - c.x) <= kHalf + give;
            if (w[i] >= 64 && d > ring.r + ring.half + give && !in_arm && !in_stem)
                faults++;
            /* no background between the arm and the ring: their pixels
             * all ink, white or grey */
            const bool band = d > ring.r - ring.half + give && d < ring.r + ring.half - give;
            const bool root = along > 0 && side < kHalf - give && d > ring.r - ring.half + give &&
                              d < ring.r + ring.half + give;
            if ((band || root) && all[i] < 64)
                faults++;
        }
    return faults;
}

float roundabout_join_bump(const Maneuver &m, bool compact)
{
    if (!ring_number(m))
        return 0;
    const int size = compact ? 28 : 48;
    const float k = size / 48.0f;
    bool mirror = false;
    const Icon ic = icon_of(m, &mirror);
    const Icon::Arc &ring = ic.arcs[0];
    const float e = drawn_exit(m.clockwise ? -m.exit_deg : m.exit_deg);
    const Pt c = ring.c, u = bearing(e), n = bearing(e - 90);
    Icon white = ic;
    white.arcs.clear();
    for (const Icon::Arc &a : ic.arcs)
        if (!a.grey)
            white.arcs.push_back(a);
    for (Icon::Stroke &s : white.strokes)
        s.grey = false;
    white.boxes.clear();
    std::vector<uint8_t> w((size_t)kTurnCardW * kTurnCardH * 4, 0);
    Canvas cw{&w, kTurnCardH};
    draw_shapes(&cw, white, mirror, 0, 0, size);
    float worst = 0;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const float ux = (x + 0.5f) / k, fy = (y + 0.5f) / k, fx = mirror ? 48 - ux : ux;
            const float dx = fx - c.x, dy = fy - c.y, d = std::sqrt(dx * dx + dy * dy);
            const float along = dx * u.x + dy * u.y, side = dx * n.x + dy * n.y;
            /* the join: the band (half a pixel in from its edges), within
             * 40 degrees of the exit, on the arm's far side */
            const float give = 0.5f / k;
            if (along < d * 0.766f || side <= 0 || d < ring.r - ring.half + give || d > ring.r + ring.half - give)
                continue;
            if (fy > c.y && std::fabs(fx - c.x) < kHalf + give)   /* the stem, where the exit is near it */
                continue;
            const float ideal = edge((kHalf - side) * k), got = w[((size_t)y * kTurnCardW + x) * 4 + 3] / 255.0f;
            if (std::fabs(got - ideal) >= 0.25f)
                worst = std::fmax(worst, std::fabs(kHalf - side) * k);
        }
    return worst;
}

}  // namespace woc
