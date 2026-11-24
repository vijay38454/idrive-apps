// SPDX-License-Identifier: GPL-3.0-or-later
/* The turn card: the next manoeuvre (an arrow, the distance to it and the
 * road it leads to) as a small RGBA picture drawn over the cluster map
 * (MapFeed::set_card), below the phone's own UI or over the top of it, as
 * turn_card_setting.h says.
 *
 * The card is kTurnCardW x kTurnCardH, straight alpha: a dark rounded plate,
 * the arrow in its left square, the distance on the first line and the road
 * on the second. Below the arrow it is compact, kTurnCardCompactH high: the
 * arrow smaller and the distance and road side by side on one line. The
 * arrows are drawn as BMW's own are: a heavy white way to
 * go, the roads not taken grey, a right-hand kind the mirror of its
 * left-hand one, a roundabout driven round clockwise the mirror of one
 * driven round anticlockwise; a roundabout's exit number (1 to 19) is in its
 * ring, in small bold digits. The text is antialiased Roboto (card_font.h),
 * accents and all; a line that does not fit ends in an ellipsis. */
#ifndef WOC_TURN_CARD_H
#define WOC_TURN_CARD_H

#include <stdint.h>
#include <string>
#include <vector>

namespace woc {

const unsigned kTurnCardW = 360, kTurnCardH = 56, kTurnCardCompactH = 34;
/* Where the card goes in the 1088x480 map, by its top-left corner: centred
 * on the part the cluster shows in full (x 306-782). On photos of a car's
 * cluster (iDrive 7, Google Maps over CarPlay, 2026-11-21) the map shows
 * from its top row down to about y 453 in the middle, the phone's position
 * arrow's circle spans about y 336-386 and the street name the phone writes
 * under it about y 389-415. Bottom is the compact card, between that name
 * and where the cluster's frame ends the map: a two-line card there (at
 * 392) covered the name, and at 344 the arrow. Top is near the map's top
 * edge: at the top of the phone's safe area (y 72) some 70 rows of map
 * showed above the card. */
const int kTurnCardX = 364, kTurnCardBottomY = 418, kTurnCardTopY = 16;

struct Maneuver {
    enum Kind {
        None,   /* no manoeuvre: no card */
        Straight,
        SlightLeft,
        Left,
        SharpLeft,
        SlightRight,
        Right,
        SharpRight,
        UTurn,        /* back over the left, as where traffic keeps right */
        UTurnRight,   /* back over the right, as where traffic keeps left */
        KeepLeft,     /* a fork's left branch, the right one grey */
        KeepRight,
        ExitLeft,     /* off a motorway to the left, the motorway grey */
        ExitRight,
        Roundabout,   /* the ring, and its exit at exit_deg */
        Ferry,        /* a ferry or a car train: a boat on the water */
        Arrive,       /* the destination: a flag */
        ArriveLeft,   /* the destination on the left of the road */
        ArriveRight,
        Other,  /* a manoeuvre whose arrow is not known (route_box.h): a plain mark */
    };
    Kind kind = None;
    /* Roundabout: where its exit leads, in degrees clockwise from straight
     * ahead (-90 left, 0 straight on, 90 right, 180 back), driven round
     * anticlockwise as where traffic keeps right, or clockwise */
    int exit_deg = 0;
    bool clockwise = false;   /* Roundabout: driven round clockwise, as where traffic keeps left */
    int exit_number = 0;      /* Roundabout: 1 the first exit, in its ring; 0 not known, none */
    std::string distance;     /* as shown: "200 m" */
    std::string road;         /* the road after the manoeuvre, or its description */

    bool operator==(const Maneuver &o) const
    {
        return kind == o.kind && exit_deg == o.exit_deg && clockwise == o.clockwise &&
               exit_number == o.exit_number && distance == o.distance && road == o.road;
    }
    bool operator!=(const Maneuver &o) const { return !(*this == o); }
};

/* The kind by its name on the control socket ("straight", "slight_left",
 * "left", "sharp_left", "slight_right", "right", "sharp_right", "uturn",
 * "uturn_right", "keep_left", "keep_right", "exit_left", "exit_right",
 * "roundabout", "arrive", "arrive_left", "arrive_right", "other", "none");
 * false for another name. */
bool maneuver_kind(const std::string &name, Maneuver::Kind *out);

/* A roundabout's neck: from its ring's outer edge to its exit's head, along
 * the exit, in the icon's units (a full card's pixels; 28/48 of them on the
 * compact card); 0 for another kind. */
float roundabout_neck(const Maneuver &m);
/* The whole pixels between a numbered roundabout's exit arm (and head) and
 * the digits in its ring, on the full or compact card; large without a
 * number. For the tests. */
float roundabout_digit_clearance(const Maneuver &m, bool compact);
/* The whole pixels between a numbered roundabout's digits and its ring;
 * large without a number. For the tests. */
float roundabout_ring_gap(const Maneuver &m, bool compact);
/* A numbered roundabout's pixels where the stem or arm shows inside the
 * ring's hole, white bulges outside the ring beyond their own widths, or
 * background shows where the arm meets the ring; 0 without a number. For
 * the tests. */
int roundabout_join_faults(const Maneuver &m, bool compact);
/* A numbered roundabout's bump where the arm meets its ring: on the arm's
 * side away from the way round, in the ring's band near the exit, the
 * farthest pixel (in pixels) from the arm's straight edge carried through
 * the band whose white is off by a quarter or more; 0 for a clean join or
 * without a number. For the tests. */
float roundabout_join_bump(const Maneuver &m, bool compact);

/* The card for m as kTurnCardW * kTurnCardH * 4 bytes of RGBA; compact,
 * kTurnCardW * kTurnCardCompactH, all on one line. */
void render_turn_card(const Maneuver &m, std::vector<uint8_t> *rgba, bool compact = false);

}  // namespace woc

#endif
