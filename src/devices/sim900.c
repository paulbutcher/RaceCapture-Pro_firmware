#include "sim900.h"
#include "cellModem.h"
#include "loggerConfig.h"
#include "mod_string.h"
#include "printk.h"
#include "LED.h"

#define TELEMETRY_SERVER_PORT "8080"

static int writeAuthJSON(Serial *serial, const char *deviceId){
	//send linefeed at slow intervals until we have the auth packet ack from server
	for (int i = 0; i < 5; i++){
		serial->put_s(" ");
		vTaskDelay(84); //250ms pause
	}
	serial->put_s("{\"cmd\":{\"schemaVer\":2,\"auth\":{\"deviceId\":\"");
	serial->put_s(deviceId);
	serial->put_s("\"}}}\n");

	if (DEBUG_LEVEL){
		pr_debug("sending auth- deviceId: ");
		pr_debug(deviceId);
		pr_debug("\r\n");
	}

	int attempts = 20;
	while (attempts-- > 0){
		const char * data = readsCell(serial, 334); //~1000ms
		if (strncmp(data, "{\"status\":\"ok\"}",15) == 0) return 0;
	}
	return -1;
}

int sim900_init_connection(DeviceConfig *config){
	LoggerConfig *loggerConfig = getWorkingLoggerConfig();
	setCellBuffer(config->buffer, config->length);
	Serial *serial = config->serial;

	pr_debug("init cell connection\r\n");
	int initResult = DEVICE_INIT_FAIL;
	if (0 == initCellModem(serial)){
		CellularConfig *cellCfg = &(loggerConfig->ConnectivityConfigs.cellularConfig);
		TelemetryConfig *telemetryConfig = &(loggerConfig->ConnectivityConfigs.telemetryConfig);
		if (0 == configureNet(serial, cellCfg->apnHost, cellCfg->apnUser, cellCfg->apnPass)){
			pr_info("cell network configured\r\n");
			if( 0 == connectNet(serial, telemetryConfig->telemetryServerHost, TELEMETRY_SERVER_PORT, 0)){
				pr_info("server connected\r\n");
				if (0 == writeAuthJSON(serial, telemetryConfig->telemetryDeviceId)){
					pr_info("server authenticated\r\n");
					initResult = DEVICE_INIT_SUCCESS;
				}
				else{
					pr_error("err: auth- token: ");
					pr_error(telemetryConfig->telemetryDeviceId);
					pr_error("\r\n");
				}
			}
			else{
				pr_error("err: server connect: ");
				pr_error(telemetryConfig->telemetryServerHost);
				pr_error("\r\n");
			}
		}
		else{
			pr_error("Failed to configure network\r\n");
		}
	}
	else{
		pr_warning("Failed to init cell connection\r\n");
	}
	return initResult;
}

int sim900_check_connection_status(DeviceConfig *config){
	setCellBuffer(config->buffer, config->length);
	int status = isNetConnectionErrorOrClosed() ? DEVICE_STATUS_DISCONNECTED : DEVICE_STATUS_NO_ERROR;
	if (DEBUG_LEVEL && status == DEVICE_STATUS_DISCONNECTED){
		pr_debug("cell disconnected\r\n");
	}
	return status;
}


