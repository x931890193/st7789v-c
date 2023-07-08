DIR_Config   = ./lib/Config
DIR_EPD      = ./lib/LCD
DIR_FONTS    = ./lib/Fonts
DIR_GUI      = ./lib/GUI
DIR_Examples = ./examples
DIR_Examples_img = ./examples/img
DIR_Examples_util = ./examples/util

DIR_CJSON    = ./lib/cjson
DIR_BIN      = ./bin

CC = riscv64-linux-gnu-gcc
MSG = -g -O0 -Wall
CFLAGS += $(MSG) $(DEBUG)


OBJ_C = $(wildcard ${DIR_EPD}/*.c ${DIR_Config}/*.c ${DIR_GUI}/*.c ${DIR_Examples}/*.c ${DIR_FONTS}/*.c ${DIR_CJSON}/*.c ${DIR_Examples_img}/*.c ${DIR_Examples_util}/*.c)
OBJ_O = $(patsubst %.c,${DIR_BIN}/%.o,$(notdir ${OBJ_C}))

TARGET = main

#USELIB = USE_BCM2835_LIB
#USELIB = USE_WIRINGPI_LIB
USELIB = USE_DEV_LIB
DEBUG = -D $(USELIB)
ifeq ($(USELIB), USE_BCM2835_LIB)
    LIB = -lbcm2835 -lm 
else ifeq ($(USELIB), USE_WIRINGPI_LIB)
    LIB = -lwiringPi -lm 
else ifeq ($(USELIB), USE_DEV_LIB)
    LIB = -lpthread -lm 
endif


${TARGET}:${OBJ_O}
	$(CC) $(CFLAGS) $(OBJ_O) -o $@ $(LIB)

${DIR_BIN}/%.o:$(DIR_Examples)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_Config) -I $(DIR_GUI) -I $(DIR_EPD) -I $(DIR_FONTS) -I $(DIR_CJSON) -I $(DIR_Examples_img) -I $(DIR_Examples_util)

${DIR_BIN}/%.o:$(DIR_EPD)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_Config)

${DIR_BIN}/%.o:$(DIR_FONTS)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@

${DIR_BIN}/%.o:$(DIR_GUI)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_Config)  -I $(DIR_EPD) -I $(DIR_Examples)

${DIR_BIN}/%.o:$(DIR_Config)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ $(LIB)

${DIR_BIN}/%.o:$(DIR_CJSON)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_CJSON)

${DIR_BIN}/%.o:$(DIR_Examples_img)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_Examples_img)

${DIR_BIN}/%.o:$(DIR_Examples_util)/%.c
	$(CC) $(CFLAGS) -c  $< -o $@ -I $(DIR_Examples_util) -I $(DIR_GUI)

clean :
	rm $(DIR_BIN)/*.* 
	rm $(TARGET) 
