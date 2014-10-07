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

   // Need 3 samples.  If we don't have 3, then we aren't ready.
   if (!isValidPoint(&(gcb->samples[2].point)))
      return false;

   const float dist1 = distPythag(&(gcb->samples[1].point), &(gcb->gc.point));
   const float dist0 = distPythag(&(gcb->samples[0].point), &(gcb->gc.point));

   return gcb->gcbCrossed = dist1 < dist0;
}

static float sq(float a) {
   return a * a;
}

struct TinyTimeLoc gc_getBoundCrossingTimeLoc(const struct GeoCircleBoundary *gcb) {
   struct TinyTimeLoc sideA;
   struct TinyTimeLoc sideB;

   // Zero out sideA because we may return it without setting values.
   memset(&sideA, 0, sizeof(struct TinyTimeLoc));

   if (!gcb->gcbCrossed)
      return sideA;

   /*
    * Now we find the two closest points to the circle center.  If done
    * correctly the middle point (index 1) will be the closest, we just need
    * to figure out which of the outter two is closer.
    */
   const float dist0 = distPythag(&(gcb->samples[0].point), &(gcb->gc.point));
   const float dist1 = distPythag(&(gcb->samples[1].point), &(gcb->gc.point));
   const float dist2 = distPythag(&(gcb->samples[2].point), &(gcb->gc.point));

   /*
    * Sanity check.  If dist1 isn't the shortest distance, then things have
    * gone wrong.  To handle those corner cases, we will simply return the
    * closest timeloc to the center of the GeoCircle.
    */
   if (dist0 < dist1)
      return gcb->samples[0];
   if (dist2 < dist1)
      return gcb->samples[2];

   /*
    * If here we are in a sane state.  Set sideA to be before the boundary
    * and sideB after.  Then do the math using law of cosines + assumption that
    * user will cross boundary at ~90 degrees to boundary.
    */
   if (dist0 < dist2) {
      sideB = gcb->samples[0];
      sideA = gcb->samples[1];
   } else {
      sideB = gcb->samples[1];
      sideA = gcb->samples[2];
   }

   /*
    * A and B are points sides A and B.  C is the center of our circle.
    */
   const float distAB = distPythag(&sideA.point, &sideB.point);
   const float distAC = distPythag(&sideA.point, &gcb->gc.point);
   const float distBC = distPythag(&sideB.point, &(gcb->gc.point));
   const float cosTheta = (sq(distAB) + sq(distAC) - sq(distBC)) /
      (2 * distAB * distAC);
   const float pct = cosTheta * distAC / distAB;

   sideA.point.latitude += (sideB.point.latitude - sideA.point.latitude) * pct;
   sideA.point.longitude += (sideB.point.longitude - sideA.point.longitude) * pct;
   sideA.millis += (tiny_millis_t) ((sideB.millis - sideA.millis) * pct);

   return sideA;
}
