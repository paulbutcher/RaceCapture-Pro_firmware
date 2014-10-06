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

#include "geoCircle.h"
#include "geopoint.h"
#include "gps.h"
#include "mod_string.h"
#include "tracks.h"

struct GeoCircle gc_createGeoCircle(const GeoPoint gp, const float r) {
   struct GeoCircle gc;

   gc.point = gp;
   gc.radius = r;

   return gc;
}

bool gc_isPointInGeoCircle(const GeoPoint point, const struct GeoCircle gc) {
   return distPythag(&point, &(gc.point)) <= gc.radius;
}

bool gc_isValidGeoCircle(const struct GeoCircle gc) {
   return isValidPoint(&(gc.point)) && gc.radius > 0.0;
}

struct GeoCircleBoundary gc_createGeoCircleBoundary(const struct GeoCircle gc) {
   struct GeoCircleBoundary gcb;

   memset(&gcb, 0, sizeof(struct GeoCircleBoundary));
   gcb.gc = gc;

   return gcb;
}

static bool isGeoCircleBoundaryValid(const struct GeoCircleBoundary *gcb) {
   return gc_isValidGeoCircle(gcb->gc);
}

bool gc_addTinyTimeLocSample(struct GeoCircleBoundary *gcb, struct TinyTimeLoc tl) {
   if (gcb->gcbCrossed)
      return true;

   if (!isGeoCircleBoundaryValid(gcb))
      return false;

   if (!gcb->gcBreached && !gc_isPointInGeoCircle(tl.point, gcb->gc))
      return false;

   // If here GeoCircle Breached.  We are recording!
   gcb->gcBreached = true;

   // TODO: Use memmove here like a civilized programmer?
   gcb->samples[2] = gcb->samples[1];
   gcb->samples[1] = gcb->samples[0];
   gcb->samples[0] = tl;

   // Cant check if we crossed the bound with only one sample.  Need more info.
   if (!isValidPoint(&(gcb->samples[1].point)))
      return false;

   const float dist1 = distPythag(&(gcb->samples[1].point), &(gcb->gc.point));
   const float dist0 = distPythag(&(gcb->samples[0].point), &(gcb->gc.point));

   return gcb->gcbCrossed = dist1 < dist0;
}

struct TinyTimeLoc gc_getBoundCrossingTimeLoc(const struct GeoCircleBoundary *gcb) {
   struct TinyTimeLoc ttl;
   return ttl;
}
