/**
 * Race Capture Pro Firmware
 *
 * Copyright (C) 2014 Autosport Labs
 *
 * This file is part of the Race Capture Pro fimrware suite
 *
 * This is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details. You should have received a copy of the GNU
 * General Public License along with this code. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Stieg
 */

#ifndef _GEOCIRCLE_H_
#define _GEOCIRCLE_H_

#include "geopoint.h"
#include "gps.h"

#include <stdbool.h>

struct GeoCircle {
   GeoPoint point;
   float radius;
};

/**
 * Creates a new GeoCircle from a point and a radius value.
 * @return A new GeoCircle.
 */
struct GeoCircle gc_createGeoCircle(const GeoPoint gp, const float radius);

/**
 * Tells us if the given point is within the bound of a given GeoCircle.  Note
 * that this is a circle only and not a sphere.  In otherwords elevation has
 * no effect on this calculation.
 * @param point The point in quesiton
 * @param gc The GeoCircle object
 * @return true if it is in side the bounds, false otherwise.
 */
bool gc_isPointInGeoCircle(const GeoPoint point, const struct GeoCircle gc);

/**
 * @return true if its a valid geoCircle, false otherwise.
 */
bool gc_isValidGeoCircle(const struct GeoCircle gc);

struct GeoCircleBoundary {
   struct TinyTimeLoc samples[3];
   struct GeoCircle gc;
   bool gcBreached;
   bool gcbCrossed;
};

/**
 * Creates a GeoCircleBoundary struct and returns it properly configured.
 * @param gc The GeoCircle that represents this boundary.
 * @return A shiny GeoCircleBoundary struct.
 */
struct GeoCircleBoundary gc_createGeoCircleBoundary(const struct GeoCircle gc);

/**
 * Adds a TimeLoc sample to our boundary detection logic.
 * @param gcb The GeoCircleBoundary structure in play.
 * @param tl The time location object being added.
 * @return true if the boundary has been crossed, false otherwise.
 * @see gc_getBoundCrossingTimeLoc for info on when it was crossed.
 */
bool gc_addTinyTimeLocSample(struct GeoCircleBoundary *gcb, struct TinyTimeLoc tl);

/**
 * @return A TinyTimeLoc structure containing the time the boundary was crossed.
 */
struct TinyTimeLoc gc_getBoundCrossingTimeLoc(const struct GeoCircleBoundary *gcb);

#endif /* _GEOCIRCLE_H_ */
