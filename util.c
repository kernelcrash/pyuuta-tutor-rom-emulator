#include "util.h"

// probably have to tweak these if the processor speed changes. Just have to be rough
void delay_us(const uint16_t us)
{
   uint32_t i = us * 60;
   while (i-- > 0) {
      __asm volatile ("nop");
   }
}

void delay_ms(const uint16_t ms)
{
   //uint32_t i = ms * 27778;
   uint32_t i = ms * 30000 *2;
   while (i-- > 0) {
      __asm volatile ("nop");
   }
}

void blink_debug_led(int delay) {
        while(1) {
                GPIOA->ODR |= GPIO_DEBUG_LED;
                delay_ms(delay);
                GPIOA->ODR &= ~(GPIO_DEBUG_LED);
                delay_ms(delay);
        }
}

uint32_t suffix_match(char *name, char *suffix) {
	if (strlen(name)>strlen(suffix)) {
		if (strncasecmp (&name[strlen(name)-strlen(suffix)],suffix,strlen(suffix)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}



// Load fname rom file into a buffer 
FRESULT load_rom(char *fname, unsigned char* buffer) {
	FRESULT res;
        FIL     fil;
        UINT BytesRead;

	memset(&fil, 0, sizeof(FIL));

	res =  f_open(&fil, fname, FA_READ);

	if (res == FR_OK) {
		res = f_read(&fil,buffer, 16384, &BytesRead);
		if (res != FR_OK) {
			blink_debug_led(3000);
		}
	}
	f_close(&fil);
	return res;
}

// This just lists the files in a directory one by one
// There are two sets of outputs; an array of 16 bit offsets, and the strings themselves.
//    index_buffer is an array of 16 bit offsets into the string 'buffer' to say where each string starts
//                 So index_buffer[0] is most likely 0x900 for where the first string starts
//                    index_buffer[1] is the offset into the buffer where the 2nd string starts
//                    The last entry in index_buffer to signify the end is always 0
//    max_files determines the max number of entries in index_buffer. It also determines that the strings buffer will
//              start immediately after the array for index_buffer
//    base_offset is added on to each 16 bit offset so that the TI can just pull an offset and immediately used it
//              with the menu address registers
// return the number of files read
uint32_t load_directory(char *dirname, uint16_t *index_buffer, uint32_t max_files, uint32_t base_offset) {
	FRESULT res;
        DIR dir;
        static FILINFO fno;
	uint32_t file_index,blanks;
	int i,j,offset;
	unsigned char *buffer;

	// make the string buffer start straight after the array
	buffer = (unsigned char *) &index_buffer[max_files];

	memset(&dir, 0, sizeof(DIR));
        res = f_opendir(&dir, (TCHAR *) dirname);
        if (res != FR_OK) {
                blink_debug_led(2000);
        }

	file_index=0;
	offset=base_offset + (max_files*2); // each offset is 2 bytes, so the string buffer starts after the laste entry
	i=0;
	while (file_index<max_files) {
		// write a 0 into the index just incase we hit the end
		index_buffer[file_index]=0;

		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == 0) {
			break;
		}
		index_buffer[file_index]=i+offset;
		j=0;
		do {
			buffer[i] = fno.fname[j];
			if (j>126) {
				buffer[i]=0;
				break;
			}
			i++;
		} while (fno.fname[j++]!=0);
		file_index++;
	}
	// Put lots of 0x00's in for the remaining entries (should roughly fill out the 16KB chunk reserved for filenames)
	for (blanks = file_index; blanks <max_files; blanks++) {
		index_buffer[blanks]=0;
	}

	res = f_closedir(&dir);
	return file_index;
}

