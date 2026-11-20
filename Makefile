CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -D_POSIX_C_SOURCE=200809L -Iinclude
LDLIBS = -lm
SOURCES = src/main.c src/command.c src/oled.c src/face.c src/face_bitmaps.c src/pca9685.c src/servo_control.c src/movement.c
OBJECTS = $(patsubst src/%.c,%.o,$(SOURCES))
TARGET = quadruped_robot

.PHONY: all web clean
all: $(TARGET) quadruped_web

web: quadruped_web

quadruped_web: src/quadruped_web.c src/web_control.c include/web_control.h movement.o servo_control.o pca9685.o face.o face_bitmaps.o oled.o
	$(CC) $(CFLAGS) -pthread -o $@ src/quadruped_web.c src/web_control.c movement.o servo_control.o pca9685.o face.o face_bitmaps.o oled.o $(LDLIBS)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

main.o: include/command.h
command.o: include/command.h include/face.h include/face_bitmaps.h include/oled.h include/servo_control.h include/pca9685.h include/movement.h
oled.o: include/oled.h
face.o: include/face.h include/face_bitmaps.h include/oled.h
face_bitmaps.o: include/face_bitmaps.h include/oled.h
pca9685.o: include/pca9685.h
servo_control.o: include/servo_control.h include/pca9685.h
movement.o: include/movement.h include/servo_control.h

%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET) quadruped_web