// SPDX-License-Identifier: GPL-3.0-or-later
/* The BMW navigation's route box: the next manoeuvres it shows on each
 * screen, as GuidanceEventList (SOME/IP 0xFB1B, major 5, instance 0x0080)
 * sends them in its event 0x8002, and the turn card's manoeuvre made of the
 * cluster's (turn_card.h). guidance_link.h receives the event.
 *
 * The payload is CommonAPI's big-endian serialisation: strings a u32 length
 * over a UTF-8 BOM, the text and a NUL; a union a u32 length, a u32 tag and
 * that many bytes. Each screen ("IC.RB.Main" the cluster's box,
 * "CID.WidgetRB.Big" the central display's) is its name, then
 *
 *   u32 (1 or 2), u32 0, u32 0, u8 (the events shown), u32 the events' bytes
 *
 * and its events, the next manoeuvre first, each a union:
 *
 *   u32 length, u32 kind  2 a turn, 4 a fork, 5 a roundabout, 6 the
 *                         destination, 7 an empty slot, 15 a ferry (BMW's
 *                         VehicleTransportation, not seen yet); others not
 *                         seen yet
 *   ...                   (33 bytes)
 *   string distance       "300"; " " or "" when none is shown
 *   u32    unit           4 metres, 1 kilometres ("1,5"), 5 yards, 3 or 2
 *                         miles, 6 feet; 0 without a distance
 *   f64    (1.0)
 *   u32, u32, u32         the text's union: lengths and tag 1
 *   string text           the road it leads to, or an instruction ("towards ...")
 *   ...                   (40 bytes, u32 4 and u32 1 at 20)
 *   u32    direction      a turn: 1 straight on, 2 left, 3 slight left,
 *                         4 sharp left, 5 right, 6 slight right, 7 sharp
 *                         right, 8 a U-turn over the right, 9 over the
 *                         left; a fork: 1 keep left, 2 keep right, 9 and
 *                         10 a fork's left and right (a motorway's slip
 *                         road), 6 and 7 off a motorway to the left and
 *                         right; the destination: 1 on the left, 2 ahead,
 *                         3 on the right
 *   u32 4, u32 1, ...
 *
 * A roundabout has, where the direction would be,
 *
 *   u16    exit angle     degrees clockwise from straight on, 1 to 360:
 *                         90 right, 180 back, 270 left, 360 straight on
 *   u8     exit           1 the first; 0 a U-turn round it
 *   u32    side           2 where traffic keeps right (round it
 *                         anticlockwise), 1 where it keeps left
 *                         (clockwise)
 *   u32 4, u32 1, ...
 *
 * These are BMW's GuidingTypes that carplay converts CarPlay's manoeuvres
 * to: its U-turns a turn's 8 (UTURN_RIGHT) where traffic keeps left and 9
 * (UTURN_LEFT) where it keeps right, which a drive where traffic keeps
 * right had (Google Maps' "make a U-turn" on the road the turn before led
 * to); its keep left and right, change motorway and a motorway's off
 * ramps a fork's 1 and 2 (BRANCH_LEFT, BRANCH_RIGHT); its on ramps a
 * fork's 9 and 10 (FORK_LEFT, FORK_RIGHT), and the off ramps 6 and 7
 * (HIGHWAY_OUT_LEFT, HIGHWAY_OUT_RIGHT) only on a head unit coded for an
 * Asian market; a roundabout's exit CarPlay's angle (360 added to one of 0
 * or less, 180 for a U-turn round it; without one 90, 360 or 270 by the
 * exit's number, mirrored where traffic keeps left), number and side (Side:
 * 1 LEFT, 2 RIGHT); the distance's unit BMW's DistanceUnit (1 KM, 2
 * MILE_UK, 3 MILE_US, 4 METER, 5 YARD, 6 FEET), carplay sending a phone's
 * miles as 3. Drives had the turns noted on the road, and a fork and
 * roundabouts the kinds 4 and 5 with these fields, where traffic keeps
 * right; the values for where it keeps left (yards, miles, 8, side 1, the
 * forks 9 and 10) are carplay's own (head unit firmware P18_23w44.2,
 * carplay 22.28.3), not seen in a route box on a drive yet. This is how a
 * car sent it (iDrive 7, Google Maps over CarPlay, 2026-11), not an
 * interface definition: a payload laid out otherwise yields no manoeuvre. */
#ifndef WOC_ROUTE_BOX_H
#define WOC_ROUTE_BOX_H

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "turn_card.h"

namespace woc {

/* The cluster's route box. */
extern const char kClusterRouteBox[];   /* "IC.RB.Main" */

/* Event kinds (the union's tags) and turn directions, as the navigation
 * sends them. */
const uint32_t kRouteBoxTurn = 2, kRouteBoxFork = 4, kRouteBoxRoundabout = 5, kRouteBoxDestination = 6,
               kRouteBoxEmpty = 7, kRouteBoxFerry = 15;

/* One event of a screen's route box. */
struct RouteBoxEvent {
    uint32_t kind = 0;        /* one of the kRouteBox kinds, or one not known */
    std::string distance;     /* "300"; blank when none is shown */
    uint32_t unit = 0;        /* 4 metres, 1 kilometres, 5 yards, 3 or 2 miles, 6 feet */
    std::string text;         /* the road, or an instruction */
    uint32_t direction = 0;   /* a roundabout: its side (2 right, 1 left); 0: not found */
    uint16_t exit_angle = 0;  /* a roundabout: 1 to 360 clockwise from straight on; 0: not found */
    uint8_t exit_number = 0;  /* a roundabout: 1 the first exit, 0 a U-turn round it */
};

/* The events of the screen named screen in an event 0x8002 payload, in
 * order, empty slots included. false: the payload does not name that screen
 * (it may name others), so it says nothing of it; true: what that screen
 * shows (no events, or empty slots only: no manoeuvre). */
bool parse_route_box(const uint8_t *p, size_t n, const std::string &screen, std::vector<RouteBoxEvent> *out);

/* The card's arrow for an event; Maneuver::Other for a kind or direction
 * not known yet, Maneuver::None for an empty slot. */
Maneuver::Kind kind_of_event(uint32_t kind, uint32_t direction);
/* Whether kind_of_event knows that event (an empty slot counts as known). */
bool event_known(uint32_t kind, uint32_t direction);

/* distance as the card shows it: "300 m" for unit 4, "1,5 km" for 1,
 * "220 yd" for 5, "0.7 mi" for 3 and 2, "350 ft" for 6, the number alone
 * for a unit not known yet, "" when it is blank. */
std::string route_box_distance(const std::string &distance, uint32_t unit);
/* Whether route_box_distance knows unit (0 to 6). */
bool unit_known(uint32_t unit);

/* The turn card's manoeuvre for a screen's events: the first one's; none
 * when every slot is empty. A roundabout's exit_deg is its exit angle from
 * -179 to 180 (270 is -90), clockwise for side 1, its exit_number the
 * exit's, and a roundabout without an exit angle the plain mark. The road
 * is the event's text without spaces at its ends, two names it puts two
 * spaces between ("Cidade A  Cidade B") joined by " · ". The arrow and the
 * exit's number come from the event's values only, never from its text.
 * false: the first slot is empty and a later one is not, as the navigation
 * sends while it rebuilds the list: the card keeps what it shows. */
bool route_box_maneuver(const std::vector<RouteBoxEvent> &events, Maneuver *out);

}  // namespace woc

#endif
