#include "gps_device.h"
#include <stdint.h>
#include <stddef.h>
#include "printk.h"
#include "mem_mang.h"
#include "taskUtil.h"
#include "printk.h"
#include "FreeRTOS.h"
#include "task.h"

#define MSG_ID_QUERY_GPS_SW_VER	0x02


#define MAX_PROVISIONING_ATTEMPTS	10
#define MAX_PAYLOAD_LEN				256
#define GPS_MSG_RX_WAIT_MS			2000
#define GPS_MESSAGE_BUFFER_LEN		1024
#define TARGET_UPDATE_RATE 			50
#define TARGET_BAUD_RATE 			921600

#define BAUD_RATE_COUNT 			2
#define BAUD_RATES 					{ \
										{921600, 8},  \
										{9600, 1}   \
									}

typedef struct _BaudRateCodes{
	uint32_t baud;
	uint8_t code;
} BaudRateCodes;

#define UPDATE_RATE_COUNT 		10
#define UPDATE_RATES 			{1, 2, 4, 5, 8, 10, 20, 25, 40, 50}

#define MSG_ID_ACK								0x83
#define MSG_ID_NACK 							0x84
#define MSG_ID_QUERY_SW_VERSION 				0x02
#define MSG_ID_SET_FACTORY_DEFAULTS				0x04
#define MSG_ID_SW_VERSION						0x80
#define MSG_ID_QUERY_POSITION_UPDATE_RATE		0x10
#define MSG_ID_CONFIGURE_POSITION_UPDATE_RATE	0x0E
#define MSG_ID_POSITION_UPDATE_RATE 			0x86
#define MSG_ID_CONFIGURE_SERIAL_PORT			0x05
#define MSG_ID_CONFIGURE_NMEA_MESSAGE			0x08

#define GGA_INTERVAL							100
#define GSA_INTERVAL							0
#define GSV_INTERVAL							0
#define GLL_INTERVAL							0
#define RMC_INTERVAL							1
#define VTG_INTERVAL							0
#define ZDA_INTERVAL							0

typedef enum{
	ATTRIBUTE_UPDATE_TO_SRAM = 0,
	ATTRIBUTE_UPDATE_TO_SRAM_AND_FLASH,
	ATTRIBUTE_TEMPORARILY
} gps_config_attribute_t;


typedef struct _Ack{
	uint8_t messageId;
	uint8_t ackId;
} Ack;

typedef struct _Nack{
	uint8_t messageId;
	uint8_t ackId;
} Nack;

typedef struct _SetFactoryDefaults{
	uint8_t messageId;
	uint8_t type;
} SetFactoryDefaults;

typedef struct _QuerySwVersion{
	uint8_t messageId;
	uint8_t softwareType;
} QuerySwVersion;

typedef struct _QueryPositionUpdateRate{
	uint8_t messageId;
} QueryPositionUpdateRate;

typedef struct _ConfigurePositionUpdateRate{
	uint8_t messageId;
	uint8_t rate;
	uint8_t attributes;
} ConfigurePositionUpdateRate;

typedef struct _PositionUpdateRate{
	uint8_t messageId;
	uint8_t rate;
} PositionUpdateRate;

typedef struct _ConfigureSerialPort{
	uint8_t messageId;
	uint8_t comPort;
	uint8_t baudRateCode;
	uint8_t attributes;
} ConfigureSerialPort;

typedef struct _ConfigureNmeaMessage{
	uint8_t messageId;
	uint8_t GGA_interval;
	uint8_t GSA_interval;
	uint8_t GSV_interval;
	uint8_t GLL_interval;
	uint8_t RMC_interval;
	uint8_t VTG_interval;
	uint8_t ZDA_interval;
	uint8_t attributes;
} ConfigureNmeaMessage;

typedef struct _GpsMessage{
	uint16_t payloadLength;
	union{
		uint8_t payload[MAX_PAYLOAD_LEN];
		uint8_t messageId;
		Ack ackMsg;
		Nack nackMsg;
		SetFactoryDefaults setFactoryDefaultsMsg;
		QuerySwVersion querySoftwareVersionMsg;
		QueryPositionUpdateRate queryPositionUpdateRate;
		ConfigurePositionUpdateRate configurePositionUpdateRate;
		ConfigureSerialPort configureSerialPort;
		PositionUpdateRate positionUpdateRate;
		ConfigureNmeaMessage configureNmeaMessage;
	};
	uint8_t checksum;
} GpsMessage;

typedef enum {
	GPS_MSG_SUCCESS = 0,
	GPS_MSG_TIMEOUT,
	GPS_MSG_NONE
} gps_msg_result_t;

typedef enum{
	GPS_COMMAND_FAIL = 0,
	GPS_COMMAND_SUCCESS
} gps_cmd_result_t;

static uint8_t calculateChecksum(GpsMessage *msg){
	uint8_t checksum = 0;
	if (msg){
		uint16_t len = msg->payloadLength;
		if (len <= MAX_PAYLOAD_LEN){
			uint8_t *payload = msg->payload;
			for (size_t i = 0; i < len; i++){
				checksum ^= payload[i];
			}
		}
	}
	return checksum;
}

static void txGpsMessage(GpsMessage *msg, Serial *serial){
	serial->put_c(0xA0);
	serial->put_c(0xA1);

	uint16_t payloadLength = msg->payloadLength;
	serial->put_c((uint8_t)payloadLength >> 8);
	serial->put_c((uint8_t)payloadLength & 0xFF);

	uint8_t *payload = msg->payload;
	while(payloadLength--){
		serial->put_c(*(payload++));
	}

	serial->put_c(msg->checksum);

	serial->put_c(0x0D);
	serial->put_c(0x0A);
}

static gps_msg_result_t rxGpsMessage(GpsMessage *msg, Serial *serial, uint8_t expectedMessageId){

	gps_msg_result_t result = GPS_MSG_NONE;
	size_t timeoutLen = msToTicks(GPS_MSG_RX_WAIT_MS);
	size_t timeoutStart = xTaskGetTickCount();

	while (result == GPS_MSG_NONE){
		uint8_t som1 = 0, som2 = 0;
		if (serial_read_byte(serial, &som1, timeoutLen) && serial_read_byte(serial, &som2, timeoutLen)){
			if (som1 == 0xA0 && som2 == 0xA1){
				uint8_t len_h = 0, len_l = 0;
				size_t len_hb = serial_read_byte(serial, &len_h, timeoutLen);
				size_t len_lb = serial_read_byte(serial, &len_l, timeoutLen);
				if (!(len_hb && len_lb)){
					result = GPS_MSG_TIMEOUT;
					break;
				}
				uint16_t len = (len_h << 8) + len_l;

				if (len <= MAX_PAYLOAD_LEN){
					msg->payloadLength = len;
					uint8_t c = 0;
					for (size_t i = 0; i < len; i++){
						if (! serial_read_byte(serial, &c, timeoutLen)){
							result = GPS_MSG_TIMEOUT;
							break;
						}
						msg->payload[i] = c;
					}
				}
				uint8_t checksum = 0;
				if (! serial_read_byte(serial, &checksum, timeoutLen)){
					result = GPS_MSG_TIMEOUT;
					break;
				}
				uint8_t calculatedChecksum = calculateChecksum(msg);
				if (calculatedChecksum == checksum){
					uint8_t eos1 = 0, eos2 = 0;
					if (! (serial_read_byte(serial, &eos1, timeoutLen) && serial_read_byte(serial, &eos2, timeoutLen))){
						result = GPS_MSG_TIMEOUT;
						break;
					}
					if (eos1 == 0x0D && eos2 == 0x0A && msg->messageId == expectedMessageId){
						result = GPS_MSG_SUCCESS;
					}
				}
			}
		}
		if (isTimeoutMs(timeoutStart, GPS_MSG_RX_WAIT_MS)){
			result = GPS_MSG_TIMEOUT;
		}
	}
	return result;
}

static void sendSetFactoryDefaults(GpsMessage *gpsMsg, Serial *serial){
	gpsMsg->messageId = MSG_ID_SET_FACTORY_DEFAULTS;
	gpsMsg->setFactoryDefaultsMsg.type = 0x01;
	gpsMsg->checksum = calculateChecksum(gpsMsg);
	txGpsMessage(gpsMsg, serial);
}

static void sendQuerySwVersion(GpsMessage * gpsMsg, Serial * serial){
	gpsMsg->messageId = MSG_ID_QUERY_SW_VERSION;
	gpsMsg->querySoftwareVersionMsg.softwareType = 0x00;
	gpsMsg->payloadLength = sizeof(QuerySwVersion);
	gpsMsg->checksum = calculateChecksum(gpsMsg);
	txGpsMessage(gpsMsg, serial);
}

static void sendQueryPositionUpdateRate(GpsMessage *gpsMsg, Serial *serial){
	gpsMsg->messageId = MSG_ID_QUERY_POSITION_UPDATE_RATE;
	gpsMsg->payloadLength = sizeof(QueryPositionUpdateRate);
	gpsMsg->checksum = calculateChecksum(gpsMsg);
	txGpsMessage(gpsMsg, serial);
}

static void sendConfigureSerialPort(GpsMessage *gpsMsg, Serial *serial, uint8_t baudRateCode){
	gpsMsg->messageId = MSG_ID_CONFIGURE_SERIAL_PORT;
	gpsMsg->configureSerialPort.baudRateCode = baudRateCode;
	gpsMsg->configureSerialPort.comPort = 0;
	gpsMsg->configureSerialPort.attributes = ATTRIBUTE_UPDATE_TO_SRAM;
	gpsMsg->payloadLength = sizeof(ConfigureSerialPort);
	gpsMsg->checksum = calculateChecksum(gpsMsg);
	txGpsMessage(gpsMsg, serial);
}

static void sendConfigureNmea(GpsMessage *gpsMsg, Serial *serial){
	gpsMsg->messageId = MSG_ID_CONFIGURE_NMEA_MESSAGE;
	gpsMsg->configureNmeaMessage.GGA_interval = GGA_INTERVAL;
	gpsMsg->configureNmeaMessage.GSA_interval = GSA_INTERVAL;
	gpsMsg->configureNmeaMessage.GSV_interval = GSV_INTERVAL;
	gpsMsg->configureNmeaMessage.GLL_interval = GLL_INTERVAL;
	gpsMsg->configureNmeaMessage.RMC_interval = RMC_INTERVAL;
	gpsMsg->configureNmeaMessage.VTG_interval = VTG_INTERVAL;
	gpsMsg->configureNmeaMessage.ZDA_interval = ZDA_INTERVAL;
	gpsMsg->configureSerialPort.attributes = ATTRIBUTE_UPDATE_TO_SRAM;
	gpsMsg->payloadLength = sizeof(ConfigureNmeaMessage);
	gpsMsg->checksum = calculateChecksum(gpsMsg);
	txGpsMessage(gpsMsg, serial);
}

static void sendConfigurePositionUpdateRate(GpsMessage *gpsMsg, Serial *serial, uint8_t updateRate){
	gpsMsg->messageId = MSG_ID_CONFIGURE_POSITION_UPDATE_RATE;
	gpsMsg->configurePositionUpdateRate.rate = updateRate;
	gpsMsg->configurePositionUpdateRate.attributes = ATTRIBUTE_UPDATE_TO_SRAM;
	gpsMsg->payloadLength = sizeof(ConfigurePositionUpdateRate);
	gpsMsg->checksum = calculateChecksum(gpsMsg);
	txGpsMessage(gpsMsg, serial);
}


uint32_t detectGpsBaudRate(GpsMessage *gpsMsg, Serial *serial){
	BaudRateCodes baud_rates[BAUD_RATE_COUNT] = BAUD_RATES;

	for (size_t i = 0; i < BAUD_RATE_COUNT; i++){
		uint32_t baudRate = baud_rates[i].baud;
		pr_info("GPS: probing baud rate ");
		pr_info_int(baudRate);
		pr_info("\r\n");
		configure_serial(SERIAL_GPS, 8, 0, 1, baudRate);
		sendQuerySwVersion(gpsMsg, serial);
		if (rxGpsMessage(gpsMsg, serial, MSG_ID_SW_VERSION) == GPS_MSG_SUCCESS){
			return baudRate;
		}
	}
	return 0;
}

static gps_cmd_result_t attemptFactoryDefaults(GpsMessage *gpsMsg, Serial *serial){
	BaudRateCodes baud_rates[BAUD_RATE_COUNT] = BAUD_RATES;

	for (size_t i = 0; i < BAUD_RATE_COUNT; i++){
		uint32_t baudRate = baud_rates[i].baud;
		pr_info("attempting factory defaults at ");
		pr_info_int(baudRate);
		pr_info("\r\n");
		configure_serial(SERIAL_GPS, 8, 0, 1, baudRate);
		serial->flush();
		sendSetFactoryDefaults(gpsMsg, serial);
		if (rxGpsMessage(gpsMsg, serial, MSG_ID_ACK) == GPS_MSG_SUCCESS && gpsMsg->ackMsg.messageId == MSG_ID_SET_FACTORY_DEFAULTS){
			pr_info("Set Factory Defaults Success\r\n");
			return GPS_COMMAND_SUCCESS;
		}
	}
	return GPS_COMMAND_FAIL;
}

static uint8_t getBaudRateCode(uint32_t baudRate){
	BaudRateCodes baud_rates[BAUD_RATE_COUNT] = BAUD_RATES;
	for (size_t i = 0; i < BAUD_RATE_COUNT; i++){
		if (baudRate == baud_rates[i].baud) return baud_rates[i].code;
	}
	return 0;
}

static gps_cmd_result_t configureBaudRate(GpsMessage *gpsMsg, Serial *serial, uint32_t targetBaudRate){
	pr_info("Configuring GPS baud rate to ");
	pr_info_int(targetBaudRate);
	pr_info(": ");
	gps_cmd_result_t result = GPS_COMMAND_FAIL;
	uint8_t baudRateCode = getBaudRateCode(targetBaudRate);
	sendConfigureSerialPort(gpsMsg, serial, baudRateCode);
	if (rxGpsMessage(gpsMsg, serial, MSG_ID_ACK) == GPS_MSG_SUCCESS){
		result = (gpsMsg->ackMsg.ackId == MSG_ID_CONFIGURE_SERIAL_PORT) ? GPS_COMMAND_SUCCESS : GPS_COMMAND_FAIL;
	}
	pr_info(result == GPS_COMMAND_SUCCESS ? "win\r\n" : "fail\r\n");
	return result;
}

static gps_cmd_result_t configureNmeaMessages(GpsMessage *gpsMsg, Serial *serial){
	pr_info("GPS: Configuring NMEA messages: ");

	gps_cmd_result_t result = GPS_COMMAND_FAIL;
	sendConfigureNmea(gpsMsg, serial);
	if (rxGpsMessage(gpsMsg, serial, MSG_ID_ACK) == GPS_MSG_SUCCESS){
		result = (gpsMsg->ackMsg.ackId == MSG_ID_CONFIGURE_NMEA_MESSAGE) ? GPS_COMMAND_SUCCESS : GPS_COMMAND_FAIL;
	}
	pr_info(result == GPS_COMMAND_SUCCESS ? "win\r\n" : "fail\r\n");
	return result;
}

static uint8_t queryPositionUpdateRate(GpsMessage *gpsMsg, Serial *serial){
	uint8_t updateRate = 0;
	sendQueryPositionUpdateRate(gpsMsg, serial);
	if (rxGpsMessage(gpsMsg, serial, MSG_ID_POSITION_UPDATE_RATE) == GPS_MSG_SUCCESS){
		updateRate = gpsMsg->positionUpdateRate.rate;
	}
	return updateRate;
}

static gps_cmd_result_t configureUpdateRate(GpsMessage *gpsMsg, Serial *serial, uint8_t targetUpdateRate){
	pr_info("Configuring GPS update rate to ");
	pr_info_int(targetUpdateRate);
	pr_info(": ");
	gps_cmd_result_t result = GPS_COMMAND_FAIL;
	sendConfigurePositionUpdateRate(gpsMsg, serial, targetUpdateRate);
	if (rxGpsMessage(gpsMsg, serial, MSG_ID_ACK) == GPS_MSG_SUCCESS){
		result = (gpsMsg->ackMsg.ackId == MSG_ID_CONFIGURE_POSITION_UPDATE_RATE) ? GPS_COMMAND_SUCCESS : GPS_COMMAND_FAIL;
	}
	pr_info(result == GPS_COMMAND_SUCCESS ? "win\r\n" : "fail\r\n");
	return result;
}

int GPS_device_provision(Serial *serial){
	GpsMessage *gpsMsg = portMalloc(sizeof(GpsMessage));
	if (gpsMsg == NULL){
		pr_error("Could not create buffer for GPS message");
		return 0;
	}
	size_t attempts = MAX_PROVISIONING_ATTEMPTS;
	size_t provisioned = 0;

	vTaskDelay(msToTicks(500));
	while(attempts-- && !provisioned){
		while(1){
			pr_info("GPS: provisioning attempt\r\n");
			uint32_t baudRate = detectGpsBaudRate(gpsMsg, serial);
			if (baudRate){
				pr_info("GPS: module detected at ");
				pr_info_int(baudRate);
				pr_info("\r\n");
				if (baudRate != TARGET_BAUD_RATE && configureBaudRate(gpsMsg, serial, TARGET_BAUD_RATE) == GPS_COMMAND_FAIL){
					pr_error("GPS: Error provisioning - could not configure baud rate\r\n");
					break;
				}
				configure_serial(SERIAL_GPS, 8, 0, 1, TARGET_BAUD_RATE);
				serial->flush();
				uint8_t updateRate = queryPositionUpdateRate(gpsMsg, serial);
				if (!updateRate){
					pr_error("GPS: Error provisioning - could not detect update rate\r\n");
					updateRate = 0;
				}
				if (updateRate != TARGET_UPDATE_RATE && configureUpdateRate(gpsMsg, serial, TARGET_UPDATE_RATE) == GPS_COMMAND_FAIL){
					pr_error("GPS: Error provisioning - could not configure update rate\r\n");
					break;
				}

				if (configureNmeaMessages(gpsMsg, serial) == GPS_COMMAND_FAIL){
					pr_error("GPS: Error provisioning - could not configure NMEA messages\r\n");
					break;
				}

				pr_info("GPS: provisioned\r\n");
				provisioned = 1;
				break;
			}
			else{
				pr_error("GPS: Error provisioning - could not detect GPS module on known baud rates\r\n");
				break;
			}
		}
		if (!provisioned && attempts == MAX_PROVISIONING_ATTEMPTS / 2){
			attemptFactoryDefaults(gpsMsg, serial);
		}
	}
	if (gpsMsg) portFree(gpsMsg);
	return provisioned;
}
