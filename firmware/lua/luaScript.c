#include "luaScript.h"
#include "memory.h"
#include "heap.h"
#include <string.h>

#define SCRIPT_PAGES 40
#define SCRIPT_LENGTH SCRIPT_PAGES * MEMORY_PAGE_SIZE

static const char g_script[SCRIPT_LENGTH] __attribute__ ((aligned (128))) __attribute__((section(".text\n\t#"))) = TEST_SCRIPT;

const char * getScript(){
	return g_script;
}

int flashScriptPage(unsigned int page, const char *data){
	
	int result = -1;
	char * scriptPageAddress = (char *)g_script;
	scriptPageAddress += (page * MEMORY_PAGE_SIZE);
	if (strlen(data) < MEMORY_PAGE_SIZE){
		//if less than the page size, copy it into an expanded buffer
		char * temp = pvPortMalloc(MEMORY_PAGE_SIZE);
		if (temp){
			strcpy(temp, data);
			result = flashWriteRegion((void *)scriptPageAddress,(void *)temp, MEMORY_PAGE_SIZE);
			vPortFree(temp);
		}
	}
	else{
		result = flashWriteRegion((void *)scriptPageAddress,(void *)data, MEMORY_PAGE_SIZE);
	}
	return result;
}

unsigned int getPageSize(){
	return MEMORY_PAGE_SIZE;
}

unsigned int getScriptPages(){
	return SCRIPT_PAGES;
}