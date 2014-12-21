/*
 * loggerSampleData.h
 *
 *  Created on: Jan 31, 2014
 *      Author: brentp
 */

#ifndef LOGGERSAMPLEDATA_H_
#define LOGGERSAMPLEDATA_H_

#include <stddef.h>

#include "loggerConfig.h"
#include "sampleRecord.h"

/**
 * Checks if we need to take a sample.  Lighter weight than populate_sample_buffer.  Ideally we
 * would calculate the next tick when we need to sample, but that is more complicated than this.
 * @param samples The ChannelSample Buffer.  This is faster than dealing with the LoggerConfig.
 * @param count The number of objects inside the LoggerConfig.
 * @param logTick The current log tick.
 * @return true if a sample is needed, false otherwise.
 */
int is_sample_needed(ChannelSample *samples, size_t count, size_t logTick);

/**
 * Gets the interval sample time from the ChannelSample array.
 * @param lm The LoggerMessage struct.
 * @return The time if found, -1 otherwise.
 */
tiny_millis_t getIntervalSampleValue(LoggerMessage *lm);

/**
 * Checks that the interval time in the LoggerMessage struct matches that within the ChannelSample
 * buffer.
 * @param lm The LoggerMessage to validate
 * @return true if its valid, false otherwise.
 */
int checkSampleTimestamp(LoggerMessage *lm);

int populate_sample_buffer(LoggerMessage *lm,  size_t count, size_t currentTicks);
void init_channel_sample_buffer(LoggerConfig *loggerConfig, ChannelSample * samples, size_t channelCount);

float get_mapped_value(float value, ScalingMap *scalingMap);

#endif /* LOGGERSAMPLEDATA_H_ */
