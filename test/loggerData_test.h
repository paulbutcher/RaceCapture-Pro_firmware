/*
 * loggerData_test.h
 *
 *  Created on: May 1, 2013
 *      Author: brent
 */

#ifndef LOGGERDATA_TEST_H_
#define LOGGERDATA_TEST_H_

#include <cppunit/extensions/HelperMacros.h>

class LoggerDataTest : public CppUnit::TestFixture
{
  CPPUNIT_TEST_SUITE( LoggerDataTest );
  CPPUNIT_TEST( testMappedValue );
  CPPUNIT_TEST_SUITE_END();

public:
  void setUp();
  void tearDown();

  void testAnalogChannels();
  void testGpsChannels();
  void testImuChannels();
  void testMappedValue();
};

#endif /* LOGGERDATA_TEST_H_ */
