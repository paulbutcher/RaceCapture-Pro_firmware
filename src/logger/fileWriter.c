/*
 * fileWriter.c
 *
 *  Created on: Feb 29, 2012
 *      Author: brent
 */
#include "fileWriter.h"
#include "task.h"
#include "semphr.h"
#include "modp_numtoa.h"
#include "sdcard.h"
#include "sampleRecord.h"
#include "loggerHardware.h"
#include "taskUtil.h"
#include "mod_string.h"
#include "printk.h"
#include "mem_mang.h"
#include "LED.h"
#include "taskUtil.h"

enum writing_status {
	WRITING_INACTIVE = 0,
	WRITING_ACTIVE
};

#define FILE_WRITER_STACK_SIZE  				200
#define SAMPLE_RECORD_QUEUE_SIZE				20
#define FILE_BUFFER_SIZE						256

#define FILENAME_LEN							13
#define MAX_LOG_FILE_INDEX 						99999
#define FLUSH_INTERVAL_MS						5000
#define ERROR_SLEEP_DELAY_MS					1000

//wait time for sample queue. can be portMAX_DELAY to wait forever, or zero to not wait at all
#define SAMPLE_QUEUE_WAIT_TIME					0

#define WRITE_SUCCESS  0
#define WRITE_FAIL     EOF

typedef struct _FileBuffer{
	char buffer[FILE_BUFFER_SIZE];
	size_t index;
} FileBuffer;

static FIL *g_logfile;
static xQueueHandle g_sampleRecordQueue = NULL;
static FileBuffer fileBuffer = {"", 0};

static int writeFileBuffer(){
	int rc = f_puts(fileBuffer.buffer, g_logfile);
	fileBuffer.index = 0;
	fileBuffer.buffer[0] = '\0';
	return rc;
}

static void appendFileBuffer(const char * data){
	size_t index = fileBuffer.index;
	char * buffer = fileBuffer.buffer + index;

	while(*data){
		*buffer++ = *data++;
		index++;
		if (index >= FILE_BUFFER_SIZE){
			pr_info("flush file buffer\r\n");
			*buffer = '\0';
			writeFileBuffer();
			index = fileBuffer.index;
			buffer = fileBuffer.buffer + index;
		}
	}
	*buffer = '\0';
	fileBuffer.index = index;
}

portBASE_TYPE queue_logfile_record(LoggerMessage * msg){
	if (NULL != g_sampleRecordQueue){
		return xQueueSend(g_sampleRecordQueue, &msg, SAMPLE_QUEUE_WAIT_TIME);
	}
	else{
		return errQUEUE_EMPTY;
	}
}

static void appendQuotedString(const char *s){
	appendFileBuffer("\"");
	appendFileBuffer(s);
	appendFileBuffer("\"");
}

static void appendInt(int num){
	char buf[12];
	modp_itoa10(num,buf);
	appendFileBuffer(buf);
}

static void appendLongLong(long long num) {
	char buf[21];
	modp_ltoa10(num, buf);
	appendFileBuffer(buf);
}

static void appendDouble(double num, int precision) {
	char buf[30];
	modp_dtoa(num, buf, precision);
	appendFileBuffer(buf);
}

static void appendFloat(float num, int precision){
	char buf[11];
	modp_ftoa(num, buf, precision);
	appendFileBuffer(buf);
}

static int writeHeaders(ChannelSample *sample, size_t channelCount){
	char *separator = "";

	for (; 0 < channelCount; channelCount--, sample++) {
      appendFileBuffer(separator);
      separator = ",";

      uint8_t precision = sample->cfg->precision;
      appendQuotedString(sample->cfg->label);
      appendFileBuffer("|");
      appendQuotedString(sample->cfg->units);
      appendFileBuffer("|");
      appendFloat(decodeSampleRate(sample->cfg->min), precision);
      appendFileBuffer("|");
      appendFloat(decodeSampleRate(sample->cfg->max), precision);
      appendFileBuffer("|");
      appendInt(decodeSampleRate(sample->cfg->sampleRate));
	}

	appendFileBuffer("\n");
	return writeFileBuffer();
}


static int writeChannelSamples(ChannelSample *sample, size_t channelCount){
	if (NULL == sample) {
      pr_debug("null sample record\r\n");
      return WRITE_FAIL;
	}

	char *separator = "";
   for (; 0 < channelCount; channelCount--, sample++) {
      appendFileBuffer(separator);
      separator = ",";

      if (!sample->populated)
         continue;

      const int precision = sample->cfg->precision;

      switch(sample->sampleData) {
      case SampleData_Float:
      case SampleData_Float_Noarg:
         appendFloat(sample->valueFloat, precision);
         break;
      case SampleData_Int:
      case SampleData_Int_Noarg:
         appendInt(sample->valueInt);
         break;
      case SampleData_LongLong:
      case SampleData_LongLong_Noarg:
         appendLongLong(sample->valueLongLong);
         break;
      case SampleData_Double:
      case SampleData_Double_Noarg:
         appendDouble(sample->valueDouble, precision);
         break;
      default:
         pr_warning("Got to unexpected location in writeChannelSamples\n");
      }
   }

   appendFileBuffer("\n");
   writeFileBuffer();

   return WRITE_SUCCESS;
}

static int openLogfile(FIL *f, char *filename){
	int rc = f_open(f,filename, FA_WRITE);
	return rc;
}

static int openNextLogfile(FIL *f, char *filename){
	int i = 0;
	int rc;
	for (; i < MAX_LOG_FILE_INDEX; i++){
		strcpy(filename,"rc_");
		char numBuf[12];
		modp_itoa10(i,numBuf);
		strcat(filename, numBuf);
		strcat(filename, ".log");
		rc = f_open(f,filename, FA_WRITE | FA_CREATE_NEW);
		if ( rc == 0 ) break;
		f_close(f);
	}
	if (i >= MAX_LOG_FILE_INDEX) return -2;
	pr_info("open ");
	pr_info(filename);
	pr_info("\r\n");
	return rc;
}

static void endLogfile(){
	pr_info("close logfile\r\n");
	f_close(g_logfile);
	UnmountFS();
}

static void flushLogfile(FIL *file){
	pr_debug("flush logfile\r\n");
	int res = f_sync(file);
	if (0 != res){
		pr_debug_int(res);
		pr_debug("=flush error\r\n");
	}
}

static int openNewLogfile(char *filename){
   int rc;

   rc = InitFS();
   if (0 != rc){
      pr_error("FS init error.  Code: ");
      pr_error_int(rc);
      pr_error("\r\n");

      LED_enable(3);
      return WRITING_INACTIVE;
   }

   //open next log file
   rc = openNextLogfile(g_logfile, filename);
   if (0 != rc){
      pr_error("File open error.  Code: ");
      pr_error_int(rc);
      pr_error("\r\n");

      LED_enable(3);
      return WRITING_INACTIVE;
   }

   return WRITING_ACTIVE;
}

void fileWriterTask(void *params){
	LoggerMessage *msg = NULL;
	unsigned int flushTimeoutInterval = 0;
	portTickType flushTimeoutStart = 0;
	size_t tick = 0;
	enum writing_status writingStatus = WRITING_INACTIVE;
	char filename[FILENAME_LEN];

	while(1){
		while(1){
			//wait for the next sample record
			xQueueReceive(g_sampleRecordQueue, &(msg), portMAX_DELAY);

			if ((LoggerMessageType_Start == msg->type || LoggerMessageType_Sample == msg->type) &&
                            WRITING_INACTIVE == writingStatus){
				pr_debug("Starting File Logging\r\n");
				LED_disable(3);
				flushTimeoutInterval = FLUSH_INTERVAL_MS;
				flushTimeoutStart = xTaskGetTickCount();
				tick = 0;
				writingStatus = openNewLogfile(filename);
			} else if (LoggerMessageType_Stop == msg->type){
				pr_info_int(tick);
				pr_info(" logfile lines written\r\n");
                                break;
			}

			if (LoggerMessageType_Sample == msg->type && WRITING_ACTIVE == writingStatus) {

//                if (!isValidLoggerMessageAge(msg)) {
//                    pr_debug("File writer Logger message too old.  Ignoring it.\r\n");
//                    continue;
//                }

			    if (0 == tick){
					writeHeaders(msg->channelSamples, msg->sampleCount);
				}
				LED_toggle(2);
				int rc = writeChannelSamples(msg->channelSamples, msg->sampleCount);
				if (rc == WRITE_FAIL){
					LED_enable(3);
					//try to recover
					f_close(g_logfile);
					UnmountFS();
					pr_error("Error writing file, recovering..\r\n");
					InitFS();
					rc = openLogfile(g_logfile, filename);
					if (0 != rc){
						pr_error("could not recover file ");
						pr_error(filename);
						pr_error("\r\n");
						break;
					}
					else{
						LED_disable(3);
					}
				}
				LED_disable(3);
				if (isTimeoutMs(flushTimeoutStart, flushTimeoutInterval)){
					flushLogfile(g_logfile);
					flushTimeoutStart = xTaskGetTickCount();
				}
				tick++;
			}
		}

                if (writingStatus != WRITING_INACTIVE)
                   endLogfile();

		writingStatus = WRITING_INACTIVE;
		delayMs(ERROR_SLEEP_DELAY_MS);
	}
}

void startFileWriterTask( int priority ){

	g_sampleRecordQueue = xQueueCreate(SAMPLE_RECORD_QUEUE_SIZE,sizeof( ChannelSample *));
	if (NULL == g_sampleRecordQueue){
		pr_error("Could not create sample record queue!");
		return;
	}
	g_logfile = pvPortMalloc(sizeof(FIL));
	if (NULL == g_logfile){
		pr_error("Could not create logfile structure!");
		return;
	}
	xTaskCreate( fileWriterTask,( signed portCHAR * ) "fileWriter", FILE_WRITER_STACK_SIZE, NULL, priority, NULL );
}
